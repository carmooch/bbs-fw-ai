using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace BBSFW.Model
{
	public enum PasCaptureState
	{
		Idle = 0,
		Running = 1,
		Done = 2,
		Overflow = 3	// edges were lost: start again
	}

	public class PasCaptureChunk
	{
		public PasCaptureState State;
		public int Count;
		public int First;
		public ushort[] Edges;
	}

	// Records 6 crank turns of raw PAS edges in the controller and downloads
	// them, 10 per request (each reply fits the firmware's 32-byte transmit
	// buffer, so the motor keeps running smoothly meanwhile).
	public static class PasCaptureSession
	{
		public static async Task<(bool ok, string message, List<ushort> edges)> Run(
			BbsfwConnection conn, IProgress<string> progress, CancellationToken ct)
		{
			var timeout = TimeSpan.FromSeconds(3);

			var started = await conn.StartPasCapture(timeout);
			if (started.Timeout || !started.Result)
			{
				return (false, "The controller didn't start the capture.", null);
			}

			// wait for it to fill: about 6 s at 60 rpm
			var waitStart = DateTime.Now;
			PasCaptureChunk chunk;
			while (true)
			{
				ct.ThrowIfCancellationRequested();
				var r = await conn.ReadPasCapture(0, timeout);
				if (r.Timeout)
				{
					return (false, "The controller stopped answering.", null);
				}
				chunk = r.Result;
				if (chunk.State == PasCaptureState.Done)
				{
					break;
				}
				if (chunk.State == PasCaptureState.Overflow)
				{
					return (false, "The controller lost edges during the capture. Try again.", null);
				}
				if (DateTime.Now - waitStart > TimeSpan.FromSeconds(60))
				{
					return (false, $"Only {chunk.Count} edges in a minute: keep turning the cranks forwards during the capture.", null);
				}
				progress?.Report($"Recording: {chunk.Count} of {EdgesPerCapture} edges (keep pedalling)");
				await Task.Delay(500, ct);
			}

			var edges = new List<ushort>();
			int count = chunk.Count;
			while (edges.Count < count)
			{
				ct.ThrowIfCancellationRequested();
				var r = await conn.ReadPasCapture(edges.Count, timeout);
				if (r.Timeout)
				{
					return (false, "The controller stopped answering during the download.", null);
				}
				edges.AddRange(r.Result.Edges.Take(Math.Min(r.Result.Edges.Length, count - edges.Count)));
				progress?.Report($"Downloading: {edges.Count} of {count} edges");
			}

			return (true, $"{edges.Count} edges recorded.", edges);
		}

		public const int EdgesPerCapture = PasCaptureAnalysis.EdgesPerRevolution * 6;
	}

	// Judges whether the pedal sensor's edges repeat steadily enough for effort
	// sensing, from a capture of raw edges (firmware effort.c):
	//
	// - every edge of both PAS channels, in the order a forward-turning crank
	//   gives them (PAS1 + PAS2 a quadrature pair);
	// - the uneven spacing of the edges (magnets, duty, phase) is learned by
	//   the firmware, so what matters is how much each edge moves from one
	//   revolution to the next ("jitter"). It's compared with the jitter the
	//   controller's 100 us timing alone causes, which the simulator models and
	//   effort sensing copes with.
	public class PasCaptureAnalysis
	{
		public const int EdgesPerRevolution = 96;
		private const ushort TicksMask = 0x3fff;
		private const ushort Pas1 = 0x4000;
		private const ushort Pas2 = 0x8000;
		private const int Window = 3;	// segments either side for the local speed

		// next (PAS1 << 1 | PAS2) state when pedalling forwards: 2 -> 3 -> 1 -> 0 -> 2
		private static readonly int[] NextForward = { 2, 0, 3, 1 };

		public int Edges { get; private set; }
		public int Revolutions { get; private set; }
		public int OrderErrors { get; private set; }
		public int Stalls { get; private set; }
		public double CadenceRpm { get; private set; }

		// time share of each channel state: PAS1 only, both, PAS2 only, neither
		public double[] StateSharePercent { get; private set; } = new double[4];

		public double JitterPercent { get; private set; }
		public double TimingJitterPercent { get; private set; }

		public double JitterRatio => TimingJitterPercent > 0 ? JitterPercent / TimingJitterPercent : 0;

		public bool Usable => Revolutions >= 3 && OrderErrors == 0 && Stalls == 0;

		public string Verdict
		{
			get
			{
				if (OrderErrors > 0)
				{
					return $"{OrderErrors} edge(s) came out of order: the two pedal sensor channels aren't a clean pair, " +
						"or edges were missed. Turn the cranks forwards steadily and capture again; if it persists, " +
						"effort sensing can't work with this sensor.";
				}
				if (Stalls > 0 || Revolutions < 3)
				{
					return "Not enough steady pedalling: turn the cranks forwards at 40-90 rpm for the whole capture (about 6 turns).";
				}
				if (JitterRatio < 1.5)
				{
					return "Clean: the edges repeat as steadily as the 100 us timing allows, which is what the simulator assumes. " +
						"Effort sensing is worth a ride test.";
				}
				if (JitterRatio < 3)
				{
					return "Somewhat noisy: the edges wander more than the simulator assumes. Effort sensing may still work, " +
						"less precisely; the ride test will tell.";
				}
				return "Noisy: the edges wander several times more than the simulator assumes, so the wobble effort sensing " +
					"looks for would be lost in it. Try again pedalling smoothly; if it stays noisy, effort sensing is unlikely to work.";
			}
		}

		public static PasCaptureAnalysis Analyse(IReadOnlyList<ushort> raw)
		{
			var a = new PasCaptureAnalysis { Edges = raw.Count };
			if (raw.Count < 2)
			{
				return a;
			}

			var ticks = raw.Select(e => (double)(e & TicksMask)).ToArray();
			var states = raw.Select(e => ((e & Pas1) != 0 ? 2 : 0) | ((e & Pas2) != 0 ? 1 : 0)).ToArray();

			var stateTicks = new double[4];
			for (int n = 1; n < raw.Count; ++n)
			{
				if (states[n] != NextForward[states[n - 1]])
				{
					a.OrderErrors++;
				}
				if ((raw[n] & TicksMask) == TicksMask)
				{
					a.Stalls++;
				}
				// the segment ending at edge n is spent in the state edge n-1 set
				stateTicks[states[n - 1]] += ticks[n];
			}

			double total = stateTicks.Sum();
			if (total > 0)
			{
				// report as PAS1 only, both, PAS2 only, neither
				a.StateSharePercent = new[] { stateTicks[2], stateTicks[3], stateTicks[1], stateTicks[0] }
					.Select(t => 100 * t / total).ToArray();
			}

			// segments 1..n: segment i ends at edge i; whole revolutions only
			int segments = raw.Count - 1;
			a.Revolutions = segments / EdgesPerRevolution;
			if (a.Revolutions < 1)
			{
				return a;
			}
			int used = a.Revolutions * EdgesPerRevolution;
			var t = new double[used];
			Array.Copy(ticks, 1, t, 0, used);

			double totalTicks = t.Sum();
			a.CadenceRpm = 60.0 / (totalTicks * 1e-4 / a.Revolutions);

			if (a.Revolutions < 2 || a.OrderErrors > 0 || a.Stalls > 0)
			{
				return a;
			}

			// the sensor's pattern: each segment's share of its revolution
			var pattern = new double[EdgesPerRevolution];
			for (int r = 0; r < a.Revolutions; ++r)
			{
				double rev = 0;
				for (int k = 0; k < EdgesPerRevolution; ++k)
				{
					rev += t[r * EdgesPerRevolution + k];
				}
				for (int k = 0; k < EdgesPerRevolution; ++k)
				{
					pattern[k] += t[r * EdgesPerRevolution + k] / rev / a.Revolutions;
				}
			}

			// each segment against what the pattern and the local speed predict;
			// the local speed comes from the segments around it, so slow changes
			// of cadence (hand-turned cranks) don't count as jitter
			double sumSq = 0, sumSqTiming = 0;
			int count = 0;
			for (int n = Window; n < used - Window; ++n)
			{
				double windowTicks = 0, windowPattern = 0;
				for (int m = n - Window; m <= n + Window; ++m)
				{
					windowTicks += t[m];
					windowPattern += pattern[m % EdgesPerRevolution];
				}
				double predicted = pattern[n % EdgesPerRevolution] * windowTicks / windowPattern;
				double e = t[n] / predicted - 1;
				sumSq += e * e;

				// 100 us timing: each edge lands up to a tick late, so a segment's
				// length is off by the difference of two such errors (variance 1/6
				// tick^2), and the window shares a seventh of it
				double timing = Math.Sqrt(1.0 / 6.0 * (1 - 1.0 / (2 * Window + 1))) / t[n];
				sumSqTiming += timing * timing;
				count++;
			}

			a.JitterPercent = 100 * Math.Sqrt(sumSq / count);
			a.TimingJitterPercent = 100 * Math.Sqrt(sumSqTiming / count);

			return a;
		}

		public string Summary()
		{
			var c = CultureInfo.InvariantCulture;
			var sb = new StringBuilder();
			sb.AppendLine(string.Format(c, "{0} edges, {1} full crank turns at {2:0} rpm", Edges, Revolutions, CadenceRpm));
			sb.AppendLine(string.Format(c, "Channels: PAS1 only {0:0}%, both {1:0}%, PAS2 only {2:0}%, neither {3:0}% of the time",
				StateSharePercent[0], StateSharePercent[1], StateSharePercent[2], StateSharePercent[3]));
			if (OrderErrors > 0)
			{
				sb.AppendLine(string.Format(c, "Out-of-order edges: {0}", OrderErrors));
			}
			if (JitterPercent > 0)
			{
				sb.AppendLine(string.Format(c, "Edge jitter: {0:0.00}% (timing alone: {1:0.00}%, ratio {2:0.0})",
					JitterPercent, TimingJitterPercent, JitterRatio));
			}
			sb.Append(Verdict);
			return sb.ToString();
		}

		public static string ToCsv(IReadOnlyList<ushort> raw)
		{
			var sb = new StringBuilder();
			sb.AppendLine("index,ticks_100us,pas1,pas2");
			for (int i = 0; i < raw.Count; ++i)
			{
				sb.AppendLine($"{i},{raw[i] & TicksMask},{((raw[i] & Pas1) != 0 ? 1 : 0)},{((raw[i] & Pas2) != 0 ? 1 : 0)}");
			}
			return sb.ToString();
		}
	}
}
