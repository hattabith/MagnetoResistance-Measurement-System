using MRMS.App.ViewModels;
using System.Windows;

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
    }
}