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
        /// Command line: WjsonViewer.exe [file] [--pointer /json/pointer]
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

        // Event handlers that loose XAML cannot hook up; removed before probing.
        private static readonly string[] EventAttributes =
            { "Click", "DragOver", "Drop", "TextChanged", "KeyDown", "Expanding", "ItemInvoked" };

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
            _window = new MainWindow();
            _window.Activate();

            string? file = null;
            string? pointer = null;
            var argv = Environment.GetCommandLineArgs();
            for (var i = 1; i < argv.Length; ++i)
            {
                if (argv[i] == "--pointer" && i + 1 < argv.Length) pointer = argv[++i];
                else file ??= argv[i];
            }
            if (file != null) await _window.OpenFileAsync(Path.GetFullPath(file), pointer);
        }
    }
}
