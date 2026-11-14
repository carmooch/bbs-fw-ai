using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using Microsoft.Win32;

namespace BBSFW.App
{
	public partial class MainWindow : Window
	{
		private readonly AppState _s = new AppState();
		private string _page = "Standard";
		private int _drag = -1;

		private static readonly Brush Accent = Br("#9DB0FF");
		private static readonly Brush Ghost = Br("#56627A");
		private static readonly Brush GridLine = Br("#1F2630");
		private static readonly Brush Danger = Br("#FF6B6B");
		private static readonly Brush MutedText = Br("#9AA5B4");
		private static readonly Brush Surface = Br("#151A21");

		public MainWindow()
		{
			InitializeComponent();
			DataContext = _s;
			for (int i = 1; i <= 9; ++i)
			{
				LevelHeader.Children.Add(new TextBlock { Text = i.ToString(), Foreground = MutedText, FontSize = 11, FontWeight = FontWeights.SemiBold, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center });
			}
			_s.MapChanged += () => { UpdatePanels(); DrawChart(); };
			_s.PropertyChanged += (o, e) => { if (e.PropertyName == nameof(AppState.HasTune) || e.PropertyName == nameof(AppState.ChangeCount)) UpdatePanels(); };
			UpdatePanels();
			// BBS-FW-AI.exe some-tune.xml: open it straight away (also what "Open with" does)
			var args = Environment.GetCommandLineArgs();
			if (args.Length > 1 && File.Exists(args[1]))
			{
				Loaded += (o, e) =>
				{
					try { _s.OpenFile(args[1]); }
					catch (Exception ex) { _s.Status = "Couldn't open that file: " + ex.Message; }
				};
			}
		}

		private static Brush Br(string hex)
		{
			var b = (Brush)new BrushConverter().ConvertFromString(hex);
			b.Freeze();
			return b;
		}

		private void UpdatePanels()
		{
			bool map = _page == "Standard" || _page == "Sport";
			EmptyPanel.Visibility = map && !_s.HasTune ? Visibility.Visible : Visibility.Collapsed;
			MapPanel.Visibility = map && _s.HasTune ? Visibility.Visible : Visibility.Collapsed;
			NotYetPanel.Visibility = map ? Visibility.Collapsed : Visibility.Visible;
			var changes = _s.Changes;
			ChangeList.ItemsSource = changes;
			NoChanges.Visibility = changes.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
		}

		// --- header ---

		private void Ports_Opened(object sender, EventArgs e) => _s.RefreshPorts();

		private async void Connect_Click(object sender, RoutedEventArgs e)
		{
			((Button)sender).IsEnabled = false;
			try { await _s.ConnectAsync(); }
			finally { ((Button)sender).IsEnabled = true; }
		}

		private void Open_Click(object sender, RoutedEventArgs e)
		{
			var dlg = new OpenFileDialog { Filter = "Tunes and backups (*.xml;*.bbstune)|*.xml;*.bbstune|All files|*.*", InitialDirectory = AppState.AppDataDir("Tunes") };
			if (dlg.ShowDialog(this) != true) return;
			try { _s.OpenFile(dlg.FileName); }
			catch (Exception ex) { _s.Status = "Couldn't open that file: " + ex.Message; }
		}

		private void Save_Click(object sender, RoutedEventArgs e)
		{
			var dlg = new SaveFileDialog { Filter = "Tune (*.xml)|*.xml", InitialDirectory = AppState.AppDataDir("Tunes"), FileName = "my-tune.xml" };
			if (dlg.ShowDialog(this) != true) return;
			_s.SaveFile(dlg.FileName);
			_s.Status = "Saved " + System.IO.Path.GetFileName(dlg.FileName);
		}

		private void Review_Click(object sender, RoutedEventArgs e)
		{
			new ReviewWindow(_s) { Owner = this }.ShowDialog();
			UpdatePanels();
		}

		private void Discard_Click(object sender, RoutedEventArgs e) => _s.Discard();

		private void Tab_Checked(object sender, RoutedEventArgs e)
		{
			if (_s == null) return;
			_s.ShowingBaseline = sender == TabBaseline;
		}

		private void Nav_Checked(object sender, RoutedEventArgs e)
		{
			if (_s == null || PageTitle == null) return;
			var rb = (RadioButton)sender;
			_page = (string)rb.Tag == "-" ? (string)rb.Content : (string)rb.Tag;
			PageTitle.Text = (string)rb.Content;
			if (_page == "Standard" || _page == "Sport") _s.Sport = _page == "Sport";
			UpdatePanels();
			DrawChart();
		}

		// --- the table ---

		private void Row_Click(object sender, MouseButtonEventArgs e)
		{
			if (((FrameworkElement)sender).Tag is MapRow row) _s.Select(row);
		}

		private void Cell_KeyDown(object sender, KeyEventArgs e)
		{
			var box = (TextBox)sender;
			var cell = (MapCell)box.Tag;
			if (e.Key == Key.Enter)
			{
				CommitCell(box);
				e.Handled = true;
			}
			else if ((e.Key == Key.Up || e.Key == Key.Down) && Keyboard.Modifiers == ModifierKeys.Shift && cell.Editable)
			{
				var def = cell.Row.Def;
				var cur = def.Get(_s.Levels(_s.Working)[cell.Level]);
				_s.SetValue(def, cell.Level, cur + (e.Key == Key.Up ? def.Step : -def.Step));
				_s.Commit();
				e.Handled = true;
			}
		}

		private void Cell_LostFocus(object sender, KeyboardFocusChangedEventArgs e) => CommitCell((TextBox)sender);

		private void CommitCell(TextBox box)
		{
			if (!(box.Tag is MapCell cell) || !cell.Editable) return;
			var text = box.Text.Trim().TrimEnd('*');
			if (text == "off") text = "0";
			if (double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var v))
			{
				var def = cell.Row.Def;
				var cur = def.Get(_s.Levels(_s.Working)[cell.Level]);
				if (Math.Abs(cur - v) > 1e-9)
				{
					_s.SetValue(def, cell.Level, v);
					_s.Commit();
					return;
				}
			}
			_s.Commit();	// put the shown value back
		}

		// --- the chart: same nine columns as the table ---

		private void Chart_SizeChanged(object sender, SizeChangedEventArgs e) => DrawChart();

		private MapRowDef ChartDef => _s.SelectedRow?.Def;

		// value -> what the chart plots, and the plot's ceiling
		private double Plot(MapRowDef def, double v) => def.IsCurrent ? _s.Amps(v) : v;

		private double Ceiling(MapRowDef def)
		{
			if (def.IsCurrent) return _s.LimitAmps;
			if (def.Max <= 100) return 100;
			var lv = _s.Levels(_s.Working);
			var top = Enumerable.Range(1, 9).Max(i => def.Get(lv[i]));
			return def.Label.StartsWith("Effort") ? Math.Max(3, top * 1.3) : Math.Max(20, top * 1.3);
		}

		private double X(int level) => (level - 0.5) / 9.0 * Chart.ActualWidth;
		private const double Top = 16, Bottom = 12;
		private double Y(double plotted, double ceiling) => Top + (1 - plotted / ceiling) * (Chart.ActualHeight - Top - Bottom);

		private void DrawChart()
		{
			Chart.Children.Clear();
			AxisLabels.Children.Clear();
			var def = ChartDef;
			var w = _s.Levels(_s.ShowingBaseline ? _s.Baseline : _s.Working);
			var b = _s.Levels(_s.Baseline);
			if (def == null || w == null || Chart.ActualWidth < 10) return;
			ChartTitle.Text = def.Label.Replace(" %", "").Replace(" ×", "").Replace(" A/s", "");
			if (!def.Chartable)
			{
				ChartUnit.Text = "not a number: pick another row";
				return;
			}
			var ceil = Ceiling(def);
			ChartUnit.Text = def.IsCurrent ? $"amps · Max Current {_s.Working.MaxCurrentAmps} A" : def.Label.Contains("%") ? "percent" : def.Label.Contains("A/s") ? "A/s (0 = global ramp)" : "× your power";

			foreach (var frac in new[] { 0.25, 0.5, 0.75 })
			{
				var y = Y(ceil * frac, ceil);
				Chart.Children.Add(new Line { X1 = 0, X2 = Chart.ActualWidth, Y1 = y, Y2 = y, Stroke = GridLine, StrokeThickness = 1 });
				AddText((ceil * frac).ToString(ceil > 30 ? "0" : "0.#", CultureInfo.InvariantCulture), 4, y - 15, MutedText);
			}
			if (def.IsCurrent)
			{
				var yMax = Y(_s.Working.MaxCurrentAmps, ceil);
				var band = new Rectangle { Width = Chart.ActualWidth, Height = Math.Max(0, yMax - Top), Fill = Br("#1AFF9F43") };
				Canvas.SetTop(band, Top);
				Chart.Children.Add(band);
				Chart.Children.Add(new Line { X1 = 0, X2 = Chart.ActualWidth, Y1 = Top, Y2 = Top, Stroke = Danger, StrokeThickness = 1.5 });
				AddText($"BBSHD limit {_s.LimitAmps} A", 4, Top + 2, Danger);
			}

			var ghost = new Polyline { Stroke = Ghost, StrokeThickness = 2, StrokeDashArray = new DoubleCollection { 3, 3 } };
			var line = new Polyline { Stroke = Accent, StrokeThickness = 3, StrokeLineJoin = PenLineJoin.Round };
			var fill = new Polygon { Fill = new LinearGradientBrush(Color.FromArgb(0x50, 0x7B, 0x95, 0xFF), Color.FromArgb(0, 0x7B, 0x95, 0xFF), 90) };
			for (int i = 1; i <= 9; ++i)
			{
				var p = new Point(X(i), Y(Plot(def, def.Get(w[i])), ceil));
				line.Points.Add(p);
				fill.Points.Add(p);
				if (b != null) ghost.Points.Add(new Point(X(i), Y(Plot(def, def.Get(b[i])), ceil)));
			}
			fill.Points.Add(new Point(X(9), Chart.ActualHeight));
			fill.Points.Add(new Point(X(1), Chart.ActualHeight));
			Chart.Children.Add(fill);
			Chart.Children.Add(ghost);
			Chart.Children.Add(line);

			for (int i = 1; i <= 9; ++i)
			{
				var v = def.Get(w[i]);
				var changed = b != null && Math.Abs(v - def.Get(b[i])) > 1e-6;
				var pt = line.Points[i - 1];
				var r = changed || i == _drag ? 7 : 5;
				var dot = new Ellipse { Width = r * 2, Height = r * 2, Fill = changed ? Accent : Surface, Stroke = changed ? Brushes.White : Accent, StrokeThickness = 2 };
				Canvas.SetLeft(dot, pt.X - r);
				Canvas.SetTop(dot, pt.Y - r);
				Chart.Children.Add(dot);
				var label = def.IsCurrent ? _s.Amps(v).ToString("0.0", CultureInfo.InvariantCulture) : def.Show(w[i], v);
				AddText(label, pt.X, pt.Y - 24, changed ? Brushes.White : MutedText, center: true);
			}
		}

		private void AddText(string text, double x, double y, Brush brush, bool center = false, bool right = false)
		{
			var t = new TextBlock { Text = text, Foreground = brush, FontSize = 11, FontFamily = new FontFamily("Cascadia Mono, Consolas") };
			t.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
			Canvas.SetLeft(t, center ? x - t.DesiredSize.Width / 2 : right ? x - t.DesiredSize.Width : x);
			Canvas.SetTop(t, y);
			Chart.Children.Add(t);
		}

		private void Chart_Down(object sender, MouseButtonEventArgs e)
		{
			var def = ChartDef;
			if (def == null || !def.Chartable || def.Put == null || _s.ShowingBaseline || !_s.HasTune) return;
			var x = e.GetPosition(Chart).X;
			_drag = Math.Max(1, Math.Min(9, (int)(x / Chart.ActualWidth * 9) + 1));
			Chart.CaptureMouse();
			Drag(e.GetPosition(Chart).Y);
		}

		private void Chart_Move(object sender, MouseEventArgs e)
		{
			if (_drag > 0 && e.LeftButton == MouseButtonState.Pressed) Drag(e.GetPosition(Chart).Y);
		}

		private void Chart_Up(object sender, MouseButtonEventArgs e)
		{
			if (_drag < 0) return;
			_drag = -1;
			Chart.ReleaseMouseCapture();
			_s.Commit();
		}

		private void Drag(double y)
		{
			var def = ChartDef;
			var ceil = Ceiling(def);
			var plotted = (1 - (y - Top) / (Chart.ActualHeight - Top - Bottom)) * ceil;
			var value = def.IsCurrent ? (_s.Working.MaxCurrentAmps == 0 ? 0 : plotted / _s.Working.MaxCurrentAmps * 100) : plotted;
			_s.SetValue(def, _drag, value);
			// update the cell under the point without rebuilding the table mid-drag
			var cell = _s.SelectedRow?.Cells.FirstOrDefault(c => c.Level == _drag);
			if (cell != null)
			{
				var lv = _s.Levels(_s.Working)[_drag];
				cell.Text = def.Show(lv, def.Get(lv));
			}
			DrawChart();
		}
	}
}
