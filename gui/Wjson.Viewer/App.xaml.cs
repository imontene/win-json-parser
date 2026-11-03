// SPDX-License-Identifier: MIT
using System;
using System.IO;
using Microsoft.UI.Xaml;

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
                throw;
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
