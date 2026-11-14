using System;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace BBSFW.App
{
	public partial class ReviewWindow : Window
	{
		private readonly AppState _s;
		private bool _done;

		public ReviewWindow(AppState s)
		{
			InitializeComponent();
			_s = s;
			var changes = s.Changes;
			Heading.Text = $"Write {changes.Count} change{(changes.Count == 1 ? "" : "s")} to the bike";
			Diff.ItemsSource = changes;
			BatteryOk.Visibility = changes.Any(c => c.IsBattery) ? Visibility.Visible : Visibility.Collapsed;

			var still = s.NotStillReason();
			Check(still == null, still == null ? "Bike still: cranks, wheel and throttle at rest" : $"Not ready: {still}");
			var w = s.Working;
			var top = Enumerable.Range(1, 9).Select(i => Math.Max(w.StandardAssistLevels[i].MaxCurrentPercent, w.SportAssistLevels[i].MaxCurrentPercent)).Max();
			Check(w.MaxCurrentAmps <= s.LimitAmps, $"Max Current {w.MaxCurrentAmps} A of {s.LimitAmps} A allowed; highest level asks for {s.Amps(top):0.0} A");
			Check(!changes.Any(c => c.IsBattery), changes.Any(c => c.IsBattery) ? "Battery voltages change: confirm on the right" : "Battery voltages unchanged", warn: true);
			if (w.StandardAssistLevels.Concat(w.SportAssistLevels).Any(l => l.EffortAssistFactor > 0))
			{
				Check(false, "Effort assist only acts once the pedal sensor pattern is learned on a ride", warn: true);
			}
			WriteButton.IsEnabled = still == null;
		}

		private void Check(bool ok, string text, bool warn = false)
		{
			var colour = ok ? "#4BE39A" : warn ? "#FFB46B" : "#FF6B6B";
			var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 4) };
			row.Children.Add(new TextBlock { Text = ok ? "✓" : "!", Foreground = (Brush)new BrushConverter().ConvertFromString(colour), FontWeight = FontWeights.Bold, Width = 18 });
			row.Children.Add(new TextBlock { Text = text, FontSize = 13, Foreground = (Brush)FindResource("Text2"), TextWrapping = TextWrapping.Wrap, MaxWidth = 430 });
			Checks.Children.Add(row);
		}

		private void Back_Click(object sender, RoutedEventArgs e) => Close();

		private async void Write_Click(object sender, RoutedEventArgs e)
		{
			if (_done)
			{
				Close();
				return;
			}
			WriteButton.IsEnabled = BackButton.IsEnabled = false;
			Result.Foreground = (Brush)FindResource("Text2");
			Result.Text = "Writing…";
			var msg = await _s.WriteAsync(BatteryOk.IsChecked == true);
			var ok = msg.StartsWith("Written and read back");
			Result.Text = msg;
			Result.Foreground = (Brush)FindResource(ok ? "Ok" : "Changed");
			if (ok && TuneName.Text.Trim().Length > 0)
			{
				var name = string.Concat(TuneName.Text.Trim().Split(Path.GetInvalidFileNameChars()));
				var path = Path.Combine(AppState.AppDataDir("Tunes"), name + ".xml");
				_s.SaveFile(path);
				if (Notes.Text.Trim().Length > 0) File.WriteAllText(Path.ChangeExtension(path, ".notes.txt"), Notes.Text);
				Result.Text += $" Saved as {name}.";
			}
			_done = ok;
			WriteButton.Content = ok ? "Done" : "Try again";
			WriteButton.IsEnabled = BackButton.IsEnabled = true;
		}
	}
}
