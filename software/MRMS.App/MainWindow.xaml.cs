using MRMS.App.ViewModels;
using System.Windows;
using System.Windows.Controls;

namespace MRMS.App
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            DataContext = new MainViewModel();
        }

        protected override async void OnClosed(EventArgs e)
        {
            if (DataContext is MainViewModel vm)
            {
                await vm.CleanupAsync();
            }

            base.OnClosed(e);
        }

        private void LogTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
        {
            if (sender is TextBox textBox)
            {
                textBox.ScrollToEnd();
            }
        }
    }
}