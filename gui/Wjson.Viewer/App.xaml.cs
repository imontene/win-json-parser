// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Linq;
using System.Xml;
using System.Xml.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Markup;

namespace Wjson.Viewer
{
    public partial class App : Application
    {
        private MainWindow? _window;

        public App()
        {
            InitializeComponent();
            UnhandledException += (_, e) => Log(e.Exception);
            AppDomain.CurrentDomain.UnhandledException += (_, e) => Log(e.ExceptionObject as Exception);
        }

        /// <summary>
        /// Crash log: %LOCALAPPDATA%\wjson\viewer.log, or the path in WJSON_VIEWER_LOG.
        /// </summary>
        internal static void Log(Exception? exception)
        {
            if (exception == null) return;
            try
            {
                var path = Environment.GetEnvironmentVariable("WJSON_VIEWER_LOG");
                if (string.IsNullOrEmpty(path))
                    path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "wjson", "viewer.log");
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.AppendAllText(path, $"[{DateTime.Now:O}] {exception}{Environment.NewLine}");
            }
            catch
            {
                // Logging must never crash the app.
            }
        }

        /// <summary>
        /// Command line: WjsonViewer.exe [file] [--pointer /json/pointer] [--compare other.json] [--lang es|en]
        /// [--self-test log.txt] (runs scripted checks on the open file, writes the results and exits: used by CI)
        /// </summary>
        protected override async void OnLaunched(LaunchActivatedEventArgs args)
        {
            try
            {
                await LaunchAsync();
            }
            catch (Exception e)
            {
                Log(e);
                if (e is XamlParseException) ProbeXaml();
                throw;
            }
        }

        private static void SetLanguage(string name)
        {
            try
            {
                Wjson.Viewer.Core.Strings.Culture = System.Globalization.CultureInfo.GetCultureInfo(name);
            }
            catch (System.Globalization.CultureNotFoundException)
            {
                // Unknown language: keep the Windows display language.
            }
        }

        // Event handlers that loose XAML cannot hook up; removed before probing.
        private static readonly string[] EventAttributes =
            { "Click", "DragOver", "Drop", "TextChanged", "SelectionChanged", "Paste", "KeyDown", "PreviewKeyDown", "Expanding", "ItemInvoked" };

        /// <summary>
        /// WinUI's XamlParseException does not say which element failed. Load
        /// every element of MainWindow.xaml on its own and log the failures; the
        /// deepest one listed is the culprit.
        /// </summary>
        private static void ProbeXaml()
        {
            try
            {
                using var stream = typeof(App).Assembly.GetManifestResourceStream("Wjson.Viewer.MainWindow.xaml");
                if (stream == null) return;
                var document = XDocument.Load(stream, LoadOptions.SetLineInfo);
                var x = XNamespace.Get("http://schemas.microsoft.com/winfx/2006/xaml");
                foreach (var element in document.Root!.Descendants())
                {
                    if (element.Name.LocalName.Contains('.')) continue; // property element, e.g. TreeView.ItemTemplate
                    var fragment = new XElement(element);
                    foreach (var e in fragment.DescendantsAndSelf().ToList())
                        e.Attributes().Where(a => EventAttributes.Contains(a.Name.LocalName) || a.Name == x + "Name").Remove();
                    try
                    {
                        XamlReader.Load(fragment.ToString());
                    }
                    catch (Exception e)
                    {
                        var line = ((IXmlLineInfo)element).LineNumber;
                        Log(new Exception($"XAML probe: <{element.Name.LocalName}> at MainWindow.xaml line {line} fails to load: {e.Message}"));
                    }
                }
            }
            catch (Exception e)
            {
                Log(e);
            }
        }

        private async System.Threading.Tasks.Task LaunchAsync()
        {
            string? file = null;
            string? pointer = null;
            string? compare = null;
            string? selfTest = null;
            var argv = Environment.GetCommandLineArgs();
            for (var i = 1; i < argv.Length; ++i)
            {
                if (argv[i] == "--pointer" && i + 1 < argv.Length) pointer = argv[++i];
                else if (argv[i] == "--compare" && i + 1 < argv.Length) compare = argv[++i];
                else if (argv[i] == "--lang" && i + 1 < argv.Length) SetLanguage(argv[++i]);
                else if (argv[i] == "--self-test" && i + 1 < argv.Length) selfTest = argv[++i];
                else file ??= argv[i];
            }

            _window = new MainWindow();
            _window.Activate();
            if (file != null) await _window.OpenFileAsync(Path.GetFullPath(file), pointer);
            if (file != null && compare != null) await _window.CompareWithAsync(Path.GetFullPath(compare));
            if (file != null && selfTest != null)
            {
                var failures = await _window.RunSelfTestAsync(Path.GetFullPath(selfTest));
                Environment.Exit(failures == 0 ? 0 : 1);
            }
        }
    }
}
