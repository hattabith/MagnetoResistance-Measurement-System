using System.Globalization;

namespace MRMS.App.Services;

// ── Result types ─────────────────────────────────────────────────────────────

public record DeviceIdentification(
    string Manufacturer,
    string Model,
    string SerialNumber,
    string FirmwareVersion);

public record AutoPickResult(
    string Tap,
    double RApproxOhm,
    double VBatteryVolts);

public record TestPulseResult(
    double RApproxOhm,
    double VMeasVolts,
    double IMeasAmps);

public record ResistanceResult(
    double ResistanceOhm,
    double VPlusVolts,
    double VMinusVolts);

public record SweepDataPoint(
    int Step,
    double VSetVolts,
    double BFieldTesla,
    double RSampleOhm,
    double VPlusVolts,
    double VMinusVolts,
    string Tap,
    string Status);

public record ScpiError(int Code, string Message);

// ── MrmsScpiClient ────────────────────────────────────────────────────────────

/// <summary>
/// Typed SCPI client for the MR-MEAS-4P instrument.
/// Wraps <see cref="SerialProtocolClient"/> with strongly-typed methods for
/// each command defined in ControlProtocolSpecification.md v1.2.
/// </summary>
public sealed class MrmsScpiClient : IDisposable
{
    private readonly SerialProtocolClient _transport;
    private static readonly CultureInfo _ci = CultureInfo.InvariantCulture;

    public MrmsScpiClient(SerialProtocolClient transport)
    {
        _transport = transport;
    }

    public bool IsConnected => _transport.IsConnected;

    // ── Connection pass-through ───────────────────────────────────────────────

    public Task ConnectAsync(string portName, int baudRate, CancellationToken ct = default)
        => _transport.ConnectAsync(portName, baudRate, ct);

    public Task DisconnectAsync()
        => _transport.DisconnectAsync();

    // ── IEEE 488.2 ────────────────────────────────────────────────────────────

    /// <summary>*IDN? — query instrument identification.</summary>
    public async Task<DeviceIdentification> GetIdentificationAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("*IDN?", ct);
        var parts = response.Split(',');
        return new DeviceIdentification(
            parts.Length > 0 ? parts[0].Trim() : string.Empty,
            parts.Length > 1 ? parts[1].Trim() : string.Empty,
            parts.Length > 2 ? parts[2].Trim() : string.Empty,
            parts.Length > 3 ? parts[3].Trim() : string.Empty);
    }

    /// <summary>*RST — reset to safe default state.</summary>
    public Task ResetAsync(CancellationToken ct = default)
        => _transport.SendWriteAsync("*RST", ct);

    /// <summary>*OPC? — wait for all previous operations to complete.</summary>
    public async Task<bool> WaitOperationCompleteAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("*OPC?", ct, timeoutMs: 10_000);
        return response.Trim() == "1";
    }

    // ── SOURce:MAGNet:VOLTage ─────────────────────────────────────────────────

    /// <summary>SOUR:MAGN:VOLT — set magnet PSU voltage (0–60 V).</summary>
    public Task SetMagnetVoltageAsync(double volts, CancellationToken ct = default)
        => _transport.SendWriteAsync(
            $"SOUR:MAGN:VOLT {volts.ToString("F1", _ci)}", ct);

    /// <summary>SOUR:MAGN:VOLT? — get current magnet PSU voltage set-point.</summary>
    public async Task<double> GetMagnetVoltageAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("SOUR:MAGN:VOLT?", ct);
        return double.Parse(response.Trim(), _ci);
    }

    // ── SOURce:SAMPle:CURRent ─────────────────────────────────────────────────

    /// <summary>SOUR:SAMP:CURR — set sample current amplitude (0.1–50 mA).</summary>
    public Task SetSampleCurrentAsync(double milliAmps, CancellationToken ct = default)
        => _transport.SendWriteAsync(
            $"SOUR:SAMP:CURR {milliAmps.ToString("F1", _ci)}", ct);

    /// <summary>SOUR:SAMP:CURR? — get sample current set-point (mA).</summary>
    public async Task<double> GetSampleCurrentAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("SOUR:SAMP:CURR?", ct);
        return double.Parse(response.Trim(), _ci);
    }

    // ── SOURce:SAMPle:PULSe:WIDTh ────────────────────────────────────────────

    /// <summary>SOUR:SAMP:PULS:WIDT — set delta-mode pulse width (10–200 ms).</summary>
    public Task SetPulseWidthAsync(int milliseconds, CancellationToken ct = default)
        => _transport.SendWriteAsync($"SOUR:SAMP:PULS:WIDT {milliseconds}", ct);

    // ── SOURce:SAMPle:TAP ─────────────────────────────────────────────────────

    /// <summary>SOUR:SAMP:TAP — select battery tap (2S/4S/…/16S/OFF).</summary>
    public Task SetTapAsync(string tap, CancellationToken ct = default)
        => _transport.SendWriteAsync($"SOUR:SAMP:TAP {tap.ToUpperInvariant()}", ct);

    /// <summary>SOUR:SAMP:TAP? — query active battery tap.</summary>
    public async Task<string> GetTapAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("SOUR:SAMP:TAP?", ct);
        return response.Trim();
    }

    // ── SOURce:SAMPle:TAP:AUTOPick ───────────────────────────────────────────

    /// <summary>
    /// SOUR:SAMP:TAP:AUTOP — auto-select optimal battery tap.
    /// Returns selected tap, approximate resistance, and battery voltage.
    /// </summary>
    public async Task<AutoPickResult> AutoPickTapAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendCommandAsync("SOUR:SAMP:TAP:AUTOP", ct, timeoutMs: 5000);
        var parts = response.Split(',');
        return new AutoPickResult(
            parts.Length > 0 ? parts[0].Trim() : string.Empty,
            parts.Length > 1 ? ParseDouble(parts[1]) : 0.0,
            parts.Length > 2 ? ParseDouble(parts[2]) : 0.0);
    }

    // ── SENSe:MAGNet:STABility ────────────────────────────────────────────────

    /// <summary>SENS:MAGN:STAB:THR — set field stability threshold (mT).</summary>
    public Task SetStabilityThresholdAsync(double mTesla, CancellationToken ct = default)
        => _transport.SendWriteAsync(
            $"SENS:MAGN:STAB:THR {mTesla.ToString("F3", _ci)}", ct);

    /// <summary>SENS:MAGN:STAB:COUN — set number of stable readings required.</summary>
    public Task SetStabilityCountAsync(int count, CancellationToken ct = default)
        => _transport.SendWriteAsync($"SENS:MAGN:STAB:COUN {count}", ct);

    /// <summary>SENS:MAGN:STAB:TIM — set stabilization timeout (100–10000 ms).</summary>
    public Task SetStabilityTimeoutAsync(int milliseconds, CancellationToken ct = default)
        => _transport.SendWriteAsync($"SENS:MAGN:STAB:TIM {milliseconds}", ct);

    // ── MEASure ───────────────────────────────────────────────────────────────

    /// <summary>MEAS:MAGN:FIEL? — read instantaneous field from Hall sensor (T).</summary>
    public async Task<double> MeasureFieldAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("MEAS:MAGN:FIEL?", ct);
        return ParseDouble(response);
    }

    /// <summary>MEAS:SAMP:TEST? — perform single test pulse; returns R, V, I.</summary>
    public async Task<TestPulseResult> MeasureTestPulseAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("MEAS:SAMP:TEST?", ct, timeoutMs: 5000);
        var parts = response.Split(',');
        return new TestPulseResult(
            parts.Length > 0 ? ParseDouble(parts[0]) : 0.0,
            parts.Length > 1 ? ParseDouble(parts[1]) : 0.0,
            parts.Length > 2 ? ParseDouble(parts[2]) : 0.0);
    }

    /// <summary>MEAS:SAMP:RES? — run full Delta-Mode cycle; returns R, V+, V−.</summary>
    public async Task<ResistanceResult> MeasureResistanceAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("MEAS:SAMP:RES?", ct, timeoutMs: 5000);
        var parts = response.Split(',');
        return new ResistanceResult(
            parts.Length > 0 ? ParseDouble(parts[0]) : 0.0,
            parts.Length > 1 ? ParseDouble(parts[1]) : 0.0,
            parts.Length > 2 ? ParseDouble(parts[2]) : 0.0);
    }

    // ── SWEEp ─────────────────────────────────────────────────────────────────

    /// <summary>SWEE:CONF — configure voltage sweep range and step.</summary>
    public Task ConfigureSweepAsync(
        double vStart, double vStop, double vStep,
        CancellationToken ct = default)
        => _transport.SendWriteAsync(
            $"SWEE:CONF {vStart.ToString("F1", _ci)},{vStop.ToString("F1", _ci)},{vStep.ToString("F1", _ci)}",
            ct);

    /// <summary>
    /// SWEE:INIT — start autonomous sweep. Each DATA line is parsed and delivered
    /// via <paramref name="onDataPoint"/>. Completes when SWEEP:COMPLETED is received.
    /// </summary>
    public Task StartSweepAsync(
        Action<SweepDataPoint> onDataPoint,
        CancellationToken ct = default,
        int overallTimeoutMs = 600_000)
        => _transport.SendSweepAsync(
            line => ParseAndDispatchDataLine(line, onDataPoint),
            ct,
            overallTimeoutMs);

    /// <summary>SWEE:ABOR — abort running sweep.</summary>
    public Task AbortSweepAsync(CancellationToken ct = default)
        => _transport.SendCommandAsync("SWEE:ABOR", ct, timeoutMs: 3000);

    // ── SYSTem ────────────────────────────────────────────────────────────────

    /// <summary>SYST:ERR? — read the next error from the FIFO queue.</summary>
    public async Task<ScpiError> GetLastErrorAsync(CancellationToken ct = default)
    {
        var response = await _transport.SendQueryAsync("SYST:ERR?", ct);
        // Format: <code>,"<message>"
        var comma = response.IndexOf(',');
        if (comma < 0)
        {
            return new ScpiError(0, response.Trim());
        }
        var codeStr = response[..comma].Trim();
        var msg     = response[(comma + 1)..].Trim().Trim('"');
        _ = int.TryParse(codeStr, out int code);
        return new ScpiError(code, msg);
    }

    // ── Private helpers ───────────────────────────────────────────────────────

    private static double ParseDouble(string s)
    {
        _ = double.TryParse(s.Trim(), NumberStyles.Float, _ci, out double v);
        return v;
    }

    private static void ParseAndDispatchDataLine(string line, Action<SweepDataPoint> dispatch)
    {
        // DATA <step>,<V_set>,<B_field>,<R_sample>,<V_plus>,<V_minus>,<Tap>,<Status>
        if (!line.StartsWith("DATA ", StringComparison.OrdinalIgnoreCase)) return;
        var parts = line[5..].Split(',');
        if (parts.Length < 8) return;

        _ = int.TryParse(parts[0].Trim(), out int step);
        dispatch(new SweepDataPoint(
            Step:        step,
            VSetVolts:   ParseDouble(parts[1]),
            BFieldTesla: ParseDouble(parts[2]),
            RSampleOhm:  ParseDouble(parts[3]),
            VPlusVolts:  ParseDouble(parts[4]),
            VMinusVolts: ParseDouble(parts[5]),
            Tap:         parts[6].Trim(),
            Status:      parts[7].Trim()));
    }

    public void Dispose()
    {
        _transport.Dispose();
    }
}
