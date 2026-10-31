using System.Windows.Controls;

namespace BBSFW.View
{
	public partial class FlashWizardView : UserControl
	{
		public FlashWizardView()
		{
			InitializeComponent();
		}

		// keep the flasher's log scrolled to the latest line
		private void OnLogChanged(object sender, TextChangedEventArgs e)
		{
			((TextBox)sender).ScrollToEnd();
		}
	}
}
