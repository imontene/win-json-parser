// Main window of wjson Viewer. UI-independent logic lives in Wjson.Viewer.Core.
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
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
    }

    public sealed partial class MainWindow : Window
    {
        private readonly DispatcherQueueTimer _validateTimer;
        private int _validationVersion;

        private string? _path;                 // null for a new document
        private DocumentFile? _largeFile;      // set when a large file is open (read-only)
        private Encoding _encoding = new UTF8Encoding(false);
        private NewlineStyle _newlines = NewlineStyle.CrLf;
        private string _cleanText = "";        // editor text at the last open/save
        private bool _dirty;
        private bool _closeConfirmed;

        private ValidationResult? _result;
        private JsonTreeNode? _selected;

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
            JsoncToggle.Label = Strings.Get("Jsonc");
            ToolTipService.SetToolTip(JsoncToggle, Strings.Get("JsoncTooltip"));
            PointerBox.PlaceholderText = Strings.Get("PointerPlaceholder");
            GoButton.Content = Strings.Get("Go");
            CopyPointerButton.Content = Strings.Get("CopyPointer");
            CopyValueButton.Content = Strings.Get("CopyValue");
            GoToErrorButton.Content = Strings.Get("GoToError");
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
            _encoding = file.Encoding;
            _newlines = file.NewlineStyle;
            _selected = null;
            ShowSelection(null);
            Tree.RootNodes.Clear();

            if (file.IsLarge)
            {
                _largeFile = file;
                SetEditorText("");
                Editor.IsReadOnly = true;
                Editor.PlaceholderText = Strings.Format("LargeFile", Strings.Size(file.Size));
            }
            else
            {
                _largeFile = null;
                Editor.IsReadOnly = false;
                Editor.PlaceholderText = Strings.Get("DropHint");
                SetEditorText(file.Text!);
            }
            _dirty = false;
            UpdateTitle();
            await ValidateAsync();
            if (!string.IsNullOrEmpty(pointer)) NavigateTo(pointer!);
        }

        private void SetEditorText(string text)
        {
            Editor.Text = text;
            _cleanText = Editor.Text; // read back: the TextBox stores line breaks as "\r"
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
                var text = Editor.Text;
                await Task.Run(() => DocumentFile.Save(path!, text, _encoding, _newlines));
            }
            catch (Exception e)
            {
                await ShowMessageAsync(Strings.Get("SaveFailed"), e.Message);
                return false;
            }
            _path = path;
            _cleanText = Editor.Text;
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

        private void Editor_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (!_dirty && Editor.Text != _cleanText)
            {
                _dirty = true;
                UpdateTitle();
            }
            _validateTimer.Stop();
            _validateTimer.Start();
        }

        private async void Jsonc_Click(object sender, RoutedEventArgs e) => await ValidateAsync();

        private async Task ValidateAsync()
        {
            _validateTimer.Stop();
            var version = ++_validationVersion;
            var jsonc = Jsonc;
            ValidationResult? result;
            if (_largeFile != null)
            {
                var file = _largeFile;
                StatusText.Text = "…";
                result = await Task.Run(() => DocumentAnalysis.AnalyzeFile(file, jsonc));
            }
            else
            {
                var text = Editor.Text;
                result = string.IsNullOrWhiteSpace(text) ? null : await Task.Run(() => DocumentAnalysis.Analyze(text, jsonc));
            }
            if (version != _validationVersion) return; // a newer validation is running
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
            SaveButton.IsEnabled = SaveAsButton.IsEnabled = _largeFile == null;

            var info = new List<string>();
            if (valid && result!.ValueCount > 0) info.Add(Strings.Format("Nodes", result.ValueCount));
            else if (valid) info.Add(Strings.Get("TooLargeForTree"));
            info.Add(_largeFile != null ? Strings.Size(_largeFile.Size) : Strings.Format("Characters", Editor.Text.Length));
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
            var text = Editor.Text;
            var index = TextPositions.IndexOf(text, error.Line, error.Column);
            Editor.Focus(FocusState.Programmatic);
            Editor.Select(index, index < text.Length ? 1 : 0);
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
                Editor.Text = indent > 0 ? WJson.Format(Editor.Text, indent, false, options) : WJson.Minify(Editor.Text, false, options);
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
            if (args.InvokedItem is TreeViewNode { Content: NodeItem item }) ShowSelection(item.Node);
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

        /// <summary>Expands the tree down to the value at a JSON Pointer and selects it.</summary>
        private void NavigateTo(string pointer)
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
