using Microsoft.Win32;
using System.Windows;

namespace BBSFW.ViewModel
{
	// Dialogs the wizard and bench view models need, behind an interface so
	// tests can answer them.
	public interface IUserPrompts
	{
		bool Confirm(string message, string title);
		void Inform(string message, string title);

		// returns the chosen path, or null if cancelled
		string AskSavePath(string title, string filter, string fileName);
	}

	public class WindowsUserPrompts : IUserPrompts
	{
		public bool Confirm(string message, string title)
		{
			return MessageBox.Show(message, title, MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
		}

		public void Inform(string message, string title)
		{
			MessageBox.Show(message, title, MessageBoxButton.OK, MessageBoxImage.Information);
		}

		public string AskSavePath(string title, string filter, string fileName)
		{
			var dialog = new SaveFileDialog { Title = title, Filter = filter, FileName = fileName };
			var result = dialog.ShowDialog();
			return result.HasValue && result.Value ? dialog.FileName : null;
		}
	}
}
