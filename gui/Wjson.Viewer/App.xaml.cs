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
        }

        /// <summary>
        /// Command line: WjsonViewer.exe [file] [--pointer /json/pointer]
        /// </summary>
        protected override async void OnLaunched(LaunchActivatedEventArgs args)
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
