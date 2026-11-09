// Main window of wjson Viewer. UI-independent logic lives in Wjson.Viewer.Core.
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Windows.Storage.Pickers;
using Wjson.Viewer.Core;

namespace Wjson.Viewer
{
    /// <summary>What the tree shows for one node (bound from the item template).</summary>
    [Microsoft.UI.Xaml.Data.Bindable]
    public sealed class NodeItem
    {
        public NodeItem(JsonTreeNode node, Brush valueBrush)
        {
            Node = node;
            Label = node.Label;
            ValueText = node.ValueText;
            ValueBrush = valueBrush;
        }

        public JsonTreeNode Node { get; }
        public string Label { get; }
        public string ValueText { get; }
        public Brush ValueBrush { get; }

        // Shown by the TreeView's default item template.
        public override string ToString() => Node.Kind == NodeKind.Range ? Label : Label + ":  " + ValueText;
    }

    /// <summary>One row of the comparison results (bound from DiffItemTemplate).</summary>
    [Microsoft.UI.Xaml.Data.Bindable]
    public sealed class DiffItem
    {
        public DiffItem(DiffEntry entry, Brush markBrush)
        {
            Entry = entry;
            MarkBrush = markBrush;
            Mark = entry.Kind switch { DiffKind.Added => "+", DiffKind.Removed => "\u2212", _ => "~" };
            Pointer = entry.Pointer.Length == 0 ? "\"\"" : entry.Pointer;
            Detail = entry.Kind switch
            {
                DiffKind.Added => JsonDiff.Preview(entry.Right),
                DiffKind.Removed => JsonDiff.Preview(entry.Left),
                _ => JsonDiff.Preview(entry.Left, 40) + "  \u2192  " + JsonDiff.Preview(entry.Right, 40),
            };
        }

        public DiffEntry Entry { get; }
        public string Mark { get; }
        public Brush MarkBrush { get; }
        public string Pointer { get; }
        public string Detail { get; }
    }

    public sealed partial class MainWindow : Window
    {
        private readonly DispatcherQueueTimer _validateTimer;
        private readonly DispatcherQueueTimer _caretTimer;
        private readonly DispatcherQueueTimer _busyTimer;
        private int _validationVersion;

        private string? _path;                 // null for a new document
        private DocumentFile? _largeFile;      // set when a large file is open (read-only)
        private Encoding _encoding = new UTF8Encoding(false);
        private NewlineStyle _newlines = NewlineStyle.CrLf;
        private string _cleanText = "";        // editor text at the last open/save
        private bool _dirty;
        private bool _closeConfirmed;

        private ValidationResult? _result;
        private string? _resultText;           // editor text that _result describes
        private JsonTreeNode? _selected;

        // Editor state (see "Editor" below).
        private string _text = "";             // editor text, line breaks as "\r"
        private readonly EditHistory _history = new EditHistory();
        private List<SyntaxSpan> _spans = new List<SyntaxSpan>(); // colors currently painted
        private bool _highlight = true;        // false while the text is above JsonSyntax.HighlightLimit
        private bool _rtfFailed;               // RTF loading changed the text once: load plain text and paint instead
        private bool _trailingBreak;           // the control reports one extra "\r" at the end of its text
        private bool _applyingHistory;
        private int _pasteLength;
        private int _ignoreSelection;          // > 0 while the editor selection is set by code

        private CancellationTokenSource? _busy;
        private DateTime _busyStart;

        public MainWindow()
        {
            InitializeComponent();
            AppWindow.Resize(new Windows.Graphics.SizeInt32(1280, 820));
            if (Microsoft.UI.Composition.SystemBackdrops.MicaController.IsSupported())
                SystemBackdrop = new MicaBackdrop();

            _validateTimer = DispatcherQueue.CreateTimer();
            _validateTimer.Interval = TimeSpan.FromMilliseconds(350);
            _validateTimer.IsRepeating = false;
            _validateTimer.Tick += async (_, _) => await ValidateAsync();

            _caretTimer = DispatcherQueue.CreateTimer();
            _caretTimer.Interval = TimeSpan.FromMilliseconds(250);
            _caretTimer.IsRepeating = false;
            _caretTimer.Tick += async (_, _) => await SyncTreeToCaretAsync();

            _busyTimer = DispatcherQueue.CreateTimer();
            _busyTimer.Interval = TimeSpan.FromMilliseconds(500);
            _busyTimer.IsRepeating = true;
            _busyTimer.Tick += (_, _) => UpdateBusyElapsed();

            try
            {
                // The tree is read-only: no drag and drop of nodes.
                Tree.CanDragItems = false;
                Tree.CanReorderItems = false;
                Tree.AllowDrop = false;
            }
            catch (Exception e)
            {
                App.Log(e);
            }

            InitializeEditor();
            ((FrameworkElement)Content).ActualThemeChanged += (_, _) => ReloadEditorColors();
            AppWindow.Closing += AppWindow_Closing;
            ApplyStrings();
            UpdateTitle();
            ShowResult(null);
        }

        // ------------------------------------------------------------------
        // Text and layout
        // ------------------------------------------------------------------

        private void ApplyStrings()
        {
            void Set(AppBarButton button, string key, string? tooltipKey = null)
            {
                button.Label = Strings.Get(key);
                ToolTipService.SetToolTip(button, Strings.Get(tooltipKey ?? key));
            }
            Set(OpenButton, "Open");
            Set(SaveButton, "Save");
            Set(SaveAsButton, "SaveAs");
            Set(FormatButton, "Format");
            Set(MinifyButton, "Minify");
            Set(CompareButton, "Compare", "CompareTooltip");
            JsoncToggle.Label = Strings.Get("Jsonc");
            ToolTipService.SetToolTip(JsoncToggle, Strings.Get("JsoncTooltip"));
            PointerBox.PlaceholderText = Strings.Get("PointerPlaceholder");
            GoButton.Content = Strings.Get("Go");
            CopyPointerButton.Content = Strings.Get("CopyPointer");
            CopyValueButton.Content = Strings.Get("CopyValue");
            GoToErrorButton.Content = Strings.Get("GoToError");
            BusyCancelButton.Content = Strings.Get("Cancel");
            Editor.PlaceholderText = Strings.Get("DropHint");
        }

        private void UpdateTitle()
        {
            var name = _path != null ? Path.GetFileName(_path) : Strings.Get("Untitled");
            Title = $"{(_dirty ? "● " : "")}{name} - {Strings.Get("AppName")}";
        }

        private bool IsDark => ((FrameworkElement)Content).ActualTheme == ElementTheme.Dark;

        /// <summary>
        /// A theme brush by key (e.g. "SystemFillColorCriticalBrush"). Looks in
        /// the theme dictionaries for the current theme, with a fallback so a
        /// missing key never crashes the app.
        /// </summary>
        private Brush ThemeBrush(string key, Windows.UI.Color fallback)
        {
            var themeKey = IsDark ? "Dark" : "Light";
            object? found = null;
            bool Search(ResourceDictionary dictionary)
            {
                if (dictionary.ThemeDictionaries.TryGetValue(themeKey, out var theme) && theme is ResourceDictionary td &&
                    td.TryGetValue(key, out found)) return true;
                if (dictionary.TryGetValue(key, out found)) return true;
                foreach (var merged in dictionary.MergedDictionaries)
                    if (Search(merged)) return true;
                return false;
            }
            try
            {
                if (Search(Application.Current.Resources) && found is Brush brush) return brush;
            }
            catch (Exception e)
            {
                App.Log(e);
            }
            return new SolidColorBrush(fallback);
        }

        private Brush SecondaryTextBrush => ThemeBrush("TextFillColorSecondaryBrush", ColorHelper.FromArgb(255, 0x80, 0x80, 0x80));
        private Brush PrimaryTextBrush => ThemeBrush("TextFillColorPrimaryBrush", IsDark ? Colors.White : Colors.Black);
        private Brush SuccessBrush => ThemeBrush("SystemFillColorSuccessBrush", ColorHelper.FromArgb(255, 0x0F, 0x7B, 0x0F));
        private Brush CriticalBrush => ThemeBrush("SystemFillColorCriticalBrush", ColorHelper.FromArgb(255, 0xC4, 0x2B, 0x1C));

        private Brush BrushFor(NodeKind kind)
        {
            var dark = IsDark;
            var color = kind switch
            {
                NodeKind.String => dark ? ColorHelper.FromArgb(255, 0x8C, 0xD9, 0x9C) : ColorHelper.FromArgb(255, 0x0B, 0x7A, 0x3E),
                NodeKind.Number => dark ? ColorHelper.FromArgb(255, 0x8C, 0xC4, 0xFF) : ColorHelper.FromArgb(255, 0x04, 0x51, 0xA5),
                NodeKind.Boolean or NodeKind.Null => dark ? ColorHelper.FromArgb(255, 0xD2, 0xA8, 0xFF) : ColorHelper.FromArgb(255, 0x80, 0x2A, 0xC8),
                _ => dark ? ColorHelper.FromArgb(255, 0xA8, 0xA8, 0xA8) : ColorHelper.FromArgb(255, 0x6B, 0x6B, 0x6B),
            };
            return new SolidColorBrush(color);
        }

        // ------------------------------------------------------------------
        // Opening and saving
        // ------------------------------------------------------------------

        public async Task OpenFileAsync(string path, string? pointer = null)
        {
            DocumentFile file;
            try
            {
                file = await Task.Run(() => DocumentFile.Load(path));
            }
            catch (Exception e)
            {
                await ShowMessageAsync(Strings.Get("OpenFailed"), e.Message);
                return;
            }

            _path = path;
            // .jsonc files (VS Code, Windows Terminal) allow comments and trailing commas.
            if (string.Equals(Path.GetExtension(path), ".jsonc", StringComparison.OrdinalIgnoreCase)) JsoncToggle.IsChecked = true;
            _encoding = file.Encoding;
            _newlines = file.NewlineStyle;
            _selected = null;
            ShowSelection(null);
            Tree.RootNodes.Clear();

            if (file.IsLarge)
            {
                _largeFile = file;
                LoadEditorText("");
                Editor.IsReadOnly = true;
                Editor.PlaceholderText = Strings.Format("LargeFile", Strings.Size(file.Size));
            }
            else
            {
                _largeFile = null;
                Editor.IsReadOnly = false;
                Editor.PlaceholderText = Strings.Get("DropHint");
                LoadEditorText(file.Text!);
            }
            _history.Clear();
            _cleanText = _text;
            _dirty = false;
            UpdateTitle();
            if (_largeFile != null) ShowResult(null); // size and encoding in the status bar while it validates
            await ValidateAsync();
            if (!string.IsNullOrEmpty(pointer)) NavigateTo(pointer!);
        }

        private async Task<bool> SaveAsync(bool saveAs)
        {
            if (_largeFile != null) return false;
            var path = _path;
            if (saveAs || path == null)
            {
                var picker = new FileSavePicker { SuggestedFileName = _path != null ? Path.GetFileNameWithoutExtension(_path) : "document" };
                picker.FileTypeChoices.Add("JSON", new List<string> { ".json" });
                picker.FileTypeChoices.Add("JSONC", new List<string> { ".jsonc" });
                WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
                var target = await picker.PickSaveFileAsync();
                if (target == null) return false;
                path = target.Path;
            }
            try
            {
                var text = _text;
                await Task.Run(() => DocumentFile.Save(path!, text, _encoding, _newlines));
            }
            catch (Exception e)
            {
                await ShowMessageAsync(Strings.Get("SaveFailed"), e.Message);
                return false;
            }
            _path = path;
            _cleanText = _text;
            _dirty = false;
            UpdateTitle();
            return true;
        }

        /// <summary>Asks to save unsaved changes. Returns false if the user cancelled.</summary>
        private async Task<bool> ConfirmDiscardAsync()
        {
            if (!_dirty) return true;
            var name = _path != null ? Path.GetFileName(_path) : Strings.Get("Untitled");
            var dialog = new ContentDialog
            {
                Title = Strings.Get("UnsavedTitle"),
                Content = Strings.Format("UnsavedMessage", name),
                PrimaryButtonText = Strings.Get("Save"),
                SecondaryButtonText = Strings.Get("DontSave"),
                CloseButtonText = Strings.Get("Cancel"),
                DefaultButton = ContentDialogButton.Primary,
                XamlRoot = Content.XamlRoot,
            };
            switch (await dialog.ShowAsync())
            {
                case ContentDialogResult.Primary: return await SaveAsync(saveAs: false);
                case ContentDialogResult.Secondary: return true;
                default: return false;
            }
        }

        private async Task ShowMessageAsync(string title, string message)
        {
            var dialog = new ContentDialog
            {
                Title = title,
                Content = message,
                CloseButtonText = Strings.Get("Ok"),
                XamlRoot = Content.XamlRoot,
            };
            await dialog.ShowAsync();
        }

        private async void AppWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
        {
            if (_closeConfirmed || !_dirty) return;
            args.Cancel = true;
            if (await ConfirmDiscardAsync())
            {
                _closeConfirmed = true;
                Close();
            }
        }

        private async void Open_Click(object sender, RoutedEventArgs e)
        {
            if (!await ConfirmDiscardAsync()) return;
            var picker = new FileOpenPicker();
            picker.FileTypeFilter.Add(".json");
            picker.FileTypeFilter.Add(".jsonc");
            picker.FileTypeFilter.Add("*");
            WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
            var file = await picker.PickSingleFileAsync();
            if (file != null) await OpenFileAsync(file.Path);
        }

        private async void Save_Click(object sender, RoutedEventArgs e) => await SaveAsync(saveAs: false);

        private async void SaveAs_Click(object sender, RoutedEventArgs e) => await SaveAsync(saveAs: true);

        private void Root_DragOver(object sender, DragEventArgs e)
        {
            if (e.DataView.Contains(StandardDataFormats.StorageItems)) e.AcceptedOperation = DataPackageOperation.Copy;
        }

        private async void Root_Drop(object sender, DragEventArgs e)
        {
            if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
            var items = await e.DataView.GetStorageItemsAsync();
            if (items.FirstOrDefault() is StorageFile file && await ConfirmDiscardAsync()) await OpenFileAsync(file.Path);
        }

        // ------------------------------------------------------------------
        // Validation
        // ------------------------------------------------------------------

        private bool Jsonc => JsoncToggle.IsChecked == true;

        private async void Jsonc_Click(object sender, RoutedEventArgs e) => await ValidateAsync();

        private async Task ValidateAsync()
        {
            _validateTimer.Stop();
            var version = ++_validationVersion;
            var jsonc = Jsonc;
            ValidationResult? result;
            var text = _text;
            if (_largeFile != null)
            {
                var file = _largeFile;
                StatusText.Text = "…";
                var cancel = BeginBusy(Strings.Format("Validating", Strings.Size(file.Size)));
                try
                {
                    void Report(AnalysisStage stage) => DispatcherQueue.TryEnqueue(() =>
                    {
                        if (_busy?.Token != cancel) return;
                        BusyText.Text = stage switch
                        {
                            AnalysisStage.Validating => Strings.Format("Validating", Strings.Size(file.Size)),
                            AnalysisStage.Reading => Strings.Get("Reading"),
                            _ => Strings.Get("BuildingTree"),
                        };
                    });
                    // WaitAsync returns as soon as the user cancels; the background work stops at its next check.
                    result = await Task.Run(() => DocumentAnalysis.AnalyzeFile(file, jsonc, Report, cancel)).WaitAsync(cancel);
                }
                catch (OperationCanceledException)
                {
                    if (version != _validationVersion) return;
                    ShowResult(null);
                    StatusText.Text = Strings.Get("Cancelled");
                    return;
                }
                finally
                {
                    EndBusy(cancel);
                }
            }
            else
            {
                result = string.IsNullOrWhiteSpace(text) ? null : await Task.Run(() => DocumentAnalysis.Analyze(text, jsonc));
            }
            if (version != _validationVersion) return; // a newer validation is running
            _resultText = text;
            ShowResult(result);
        }

        private void ShowResult(ValidationResult? result)
        {
            _result = result;
            var valid = result?.IsValid == true;

            if (result == null)
            {
                StatusIcon.Glyph = "\uE946"; // info
                StatusIcon.Foreground = SecondaryTextBrush;
                StatusText.Text = Strings.Get("Empty");
                Tree.RootNodes.Clear();
            }
            else if (valid)
            {
                StatusIcon.Glyph = "\uE73E"; // check mark
                StatusIcon.Foreground = SuccessBrush;
                StatusText.Text = Strings.Get("Valid");
                if (result.Root != null) RebuildTree(result.Root);
                else Tree.RootNodes.Clear();
                Tree.Opacity = 1;
            }
            else
            {
                StatusIcon.Glyph = "\uE783"; // error
                StatusIcon.Foreground = CriticalBrush;
                StatusText.Text = result.Message;
                Tree.Opacity = 0.45; // the tree shows the last valid version
            }

            GoToErrorButton.Visibility = result != null && !result.IsValid && _largeFile == null ? Visibility.Visible : Visibility.Collapsed;
            FormatButton.IsEnabled = MinifyButton.IsEnabled = valid && _largeFile == null;
            CompareButton.IsEnabled = valid && result!.Root != null;
            SaveButton.IsEnabled = SaveAsButton.IsEnabled = _largeFile == null;

            var info = new List<string>();
            if (valid && result!.ValueCount > 0) info.Add(Strings.Format("Nodes", result.ValueCount));
            else if (valid) info.Add(Strings.Get("TooLargeForTree"));
            info.Add(_largeFile != null ? Strings.Size(_largeFile.Size) : Strings.Format("Characters", _text.Length));
            if (!_highlight) info.Add(Strings.Get("NoHighlight"));
            info.Add(EncodingName());
            info.Add(_newlines == NewlineStyle.CrLf ? "CRLF" : "LF");
            InfoText.Text = string.Join("  ·  ", info);
        }

        private string EncodingName()
        {
            var bom = _encoding.GetPreamble().Length > 0;
            return _encoding switch
            {
                UnicodeEncoding u when u.CodePage == 1201 => "UTF-16 BE",
                UnicodeEncoding => "UTF-16 LE",
                UTF8Encoding when bom => "UTF-8 BOM",
                _ => "UTF-8",
            };
        }

        private void GoToError_Click(object sender, RoutedEventArgs e)
        {
            if (_result?.Error is not { } error || _largeFile != null) return;
            var text = _text;
            var index = TextPositions.IndexOf(text, error.Line, error.Column);
            Editor.Focus(FocusState.Programmatic);
            SelectInEditor(index, index < text.Length ? index + 1 : index);
        }

        // ------------------------------------------------------------------
        // Compare
        // ------------------------------------------------------------------

        private async void Compare_Click(object sender, RoutedEventArgs e)
        {
            var picker = new FileOpenPicker();
            picker.FileTypeFilter.Add(".json");
            picker.FileTypeFilter.Add(".jsonc");
            picker.FileTypeFilter.Add("*");
            WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
            var file = await picker.PickSingleFileAsync();
            if (file != null) await CompareWithAsync(file.Path);
        }

        /// <summary>Compares the current document (left) with another file (right) and shows the differences.</summary>
        public async Task CompareWithAsync(string otherPath)
        {
            var left = _result?.Root;
            if (_result?.IsValid != true || left == null)
            {
                await ShowMessageAsync(Strings.Get("Compare"), Strings.Get("CompareNeedsValid"));
                return;
            }

            var name = Path.GetFileName(otherPath);
            var options = DocumentAnalysis.Options(Jsonc);
            DiffResult diff;
            var cancel = BeginBusy(Strings.Format("Comparing", name));
            try
            {
                diff = await Task.Run(() => JsonDiff.Compare(left.Value, WJson.ParseFile(otherPath, options))).WaitAsync(cancel);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (JsonParseException ex)
            {
                var where = ex.Error.Line > 0 ? Strings.Format("ErrorAt", ex.Error.Line, ex.Error.Column, Strings.Error(ex.Error))
                                              : Strings.Error(ex.Error);
                await ShowMessageAsync(Strings.Get("Compare"), Strings.Format("OtherInvalid", name, where));
                return;
            }
            catch (Exception ex)
            {
                await ShowMessageAsync(Strings.Get("OpenFailed"), ex.Message);
                return;
            }
            finally
            {
                EndBusy(cancel);
            }
            await ShowDiffAsync(diff, name);
        }

        private async Task ShowDiffAsync(DiffResult diff, string otherName)
        {
            var panel = new StackPanel { Spacing = 8 };
            var dialog = new ContentDialog
            {
                Title = Strings.Format("CompareTitle", otherName),
                Content = panel,
                CloseButtonText = Strings.Get("Close"),
                XamlRoot = Content.XamlRoot,
            };
            dialog.Resources["ContentDialogMaxWidth"] = 900.0;

            if (diff.AreEqual)
            {
                panel.Children.Add(new TextBlock { Text = Strings.Get("CompareEqual") });
                await dialog.ShowAsync();
                return;
            }

            var summary = Strings.Format("DiffSummary", diff.Count(DiffKind.Added), diff.Count(DiffKind.Removed),
                                         diff.Count(DiffKind.Changed));
            if (diff.Truncated) summary += "  " + Strings.Format("DiffTruncated", JsonDiff.MaxEntries);
            panel.Children.Add(new TextBlock { Text = summary, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
            panel.Children.Add(new TextBlock { Text = Strings.Get("DiffHint"), Foreground = SecondaryTextBrush });

            var added = BrushFor(NodeKind.String);
            var removed = CriticalBrush;
            var changed = BrushFor(NodeKind.Number);
            var items = diff.Entries.Select(entry => new DiffItem(entry, entry.Kind switch
            {
                DiffKind.Added => added,
                DiffKind.Removed => removed,
                _ => changed,
            })).ToList();

            var list = new ListView
            {
                ItemsSource = items,
                ItemTemplate = (DataTemplate)Root.Resources["DiffItemTemplate"],
                IsItemClickEnabled = true,
                SelectionMode = ListViewSelectionMode.None,
                MaxHeight = 420,
                MinWidth = 640,
            };
            list.ItemClick += (_, args) =>
            {
                if (args.ClickedItem is not DiffItem item) return;
                dialog.Hide();
                // Added values exist only in the other file: show where they would go.
                var pointer = item.Entry.LeftPointer ?? ParentPointer(item.Entry.RightPointer ?? "");
                NavigateTo(pointer);
            };
            panel.Children.Add(list);
            await dialog.ShowAsync();
        }

        // "/a/b/3" -> "/a/b"; the parent of a top-level member is the root "".
        private static string ParentPointer(string pointer)
        {
            var slash = pointer.LastIndexOf('/');
            return slash <= 0 ? "" : pointer.Substring(0, slash);
        }

        // ------------------------------------------------------------------
        // Format / minify
        // ------------------------------------------------------------------

        private void Format_Click(object sender, RoutedEventArgs e) => Reformat(indent: 2);

        private void Minify_Click(object sender, RoutedEventArgs e) => Reformat(indent: 0);

        private void Reformat(int indent)
        {
            if (_result?.IsValid != true || _largeFile != null) return;
            var options = DocumentAnalysis.Options(Jsonc);
            try
            {
                var text = indent > 0 ? WJson.Format(_text, indent, false, options) : WJson.Minify(_text, false, options);
                ReplaceEditorText(text);
            }
            catch (JsonParseException ex)
            {
                StatusText.Text = Strings.Error(ex.Error);
            }
        }

        // ------------------------------------------------------------------
        // Tree
        // ------------------------------------------------------------------

        private TreeViewNode CreateTreeNode(JsonTreeNode node) => new TreeViewNode
        {
            Content = new NodeItem(node, BrushFor(node.Kind)),
            HasUnrealizedChildren = node.HasChildren,
        };

        private void Fill(TreeViewNode treeNode)
        {
            if (!treeNode.HasUnrealizedChildren) return;
            treeNode.HasUnrealizedChildren = false;
            foreach (var child in ((NodeItem)treeNode.Content).Node.GetChildren())
                treeNode.Children.Add(CreateTreeNode(child));
        }

        private void Tree_Expanding(TreeView sender, TreeViewExpandingEventArgs args) => Fill(args.Node);

        private static string Key(JsonTreeNode node) => node.Kind == NodeKind.Range ? node.Pointer + "#" + node.Label : node.Pointer;

        /// <summary>Replaces the tree, keeping the nodes that were expanded (by pointer).</summary>
        private void RebuildTree(JsonTreeNode root)
        {
            var expanded = new HashSet<string>();
            void Collect(TreeViewNode n)
            {
                if (!n.IsExpanded) return;
                expanded.Add(Key(((NodeItem)n.Content).Node));
                foreach (var child in n.Children) Collect(child);
            }
            foreach (var n in Tree.RootNodes) Collect(n);
            if (expanded.Count == 0) expanded.Add(Key(root)); // first time: expand the root

            Tree.RootNodes.Clear();
            var rootNode = CreateTreeNode(root);
            Tree.RootNodes.Add(rootNode);

            void Restore(TreeViewNode n)
            {
                if (!expanded.Contains(Key(((NodeItem)n.Content).Node))) return;
                Fill(n);
                n.IsExpanded = true;
                foreach (var child in n.Children) Restore(child);
            }
            Restore(rootNode);

            if (_selected != null)
            {
                var pointer = _selected.Pointer;
                _selected = null;
                ShowSelection(JsonTree.FindPath(root, pointer)?.Last());
            }
        }

        private void Tree_ItemInvoked(TreeView sender, TreeViewItemInvokedEventArgs args)
        {
            if (args.InvokedItem is TreeViewNode { Content: NodeItem item })
            {
                ShowSelection(item.Node);
                SelectInEditor(item.Node.Pointer);
            }
        }

        private void ShowSelection(JsonTreeNode? node)
        {
            _selected = node;
            CopyPointerButton.IsEnabled = node != null;
            CopyValueButton.IsEnabled = node != null && node.Kind != NodeKind.Range;
            SelectionText.Foreground = PrimaryTextBrush;
            SelectionText.Text = node == null ? "" : node.Pointer.Length == 0 ? "\"\"  (" + Strings.Get("Root") + ")" : node.Pointer;
        }

        private void PointerBox_KeyDown(object sender, KeyRoutedEventArgs e)
        {
            if (e.Key != Windows.System.VirtualKey.Enter) return;
            e.Handled = true;
            NavigateTo(PointerBox.Text.Trim());
        }

        private void Go_Click(object sender, RoutedEventArgs e) => NavigateTo(PointerBox.Text.Trim());

        /// <summary>
        /// Expands the tree down to the value at a JSON Pointer and selects it,
        /// and also in the editor unless the request comes from the editor.
        /// </summary>
        private void NavigateTo(string pointer, bool fromEditor = false)
        {
            PointerBox.Text = pointer;
            var root = _result?.Root;
            if (root == null || Tree.RootNodes.Count == 0) return;

            if (JsonTree.ParsePointer(pointer) == null)
            {
                ShowNavigationError(Strings.Get("PointerInvalid"));
                return;
            }
            var path = JsonTree.FindPath(root, pointer);
            if (path == null)
            {
                ShowNavigationError(Strings.Format("PointerNotFound", pointer));
                return;
            }

            var current = Tree.RootNodes[0];
            foreach (var step in path.Skip(1))
            {
                Fill(current);
                current.IsExpanded = true;
                current = current.Children.First(c => ((NodeItem)c.Content).Node == step);
            }
            Tree.SelectedNode = current;
            ShowSelection(path[path.Count - 1]);
            if (!fromEditor) SelectInEditor(pointer);

            var target = current;
            DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () =>
            {
                if (Tree.ContainerFromNode(target) is UIElement container) container.StartBringIntoView();
            });
        }

        private void ShowNavigationError(string message)
        {
            ShowSelection(null);
            SelectionText.Foreground = CriticalBrush;
            SelectionText.Text = message;
        }

        private void CopyPointer_Click(object sender, RoutedEventArgs e)
        {
            if (_selected != null) CopyText(_selected.Pointer);
        }

        private void CopyValue_Click(object sender, RoutedEventArgs e)
        {
            if (_selected != null && _selected.Kind != NodeKind.Range) CopyText(JsonWriter.Write(_selected.Value, 2));
        }

        private static void CopyText(string text)
        {
            var package = new DataPackage();
            package.SetText(text);
            Clipboard.SetContent(package);
        }
    }
}
