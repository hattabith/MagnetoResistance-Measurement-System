using System.Collections.ObjectModel;
using System.Globalization;
using System.IO.Ports;
using System.Linq;
using System.Windows.Input;
using MRMS.App.Commands;
using MRMS.App.Services;

namespace MRMS.App.ViewModels;

// ── Sweep row displayed in the data grid ─────────────────────────────────────

public sealed class SweepRowViewModel
{
    public int    Step        { get; init; }
    public double VSetVolts   { get; init; }
    public double BFieldTesla { get; init; }
    public double RSampleOhm  { get; init; }
    public double VPlusVolts  { get; init; }
    public double VMinusVolts { get; init; }
    public string Tap         { get; init; } = string.Empty;
    public string Status      { get; init; } = string.Empty;
}

// ── Main view-model ───────────────────────────────────────────────────────────

public sealed class MainViewModel : ObservableObject
{
    private static readonly CultureInfo _ci = CultureInfo.InvariantCulture;

    private readonly MrmsScpiClient _client;

    // Connection
    private string? _selectedPort;
    private int     _selectedBaudRate  = 115200;
    private bool    _isConnected;
    private string  _instrumentId      = "—";

    // Configuration
    private double _magnetVoltage      = 0.0;
    private double _sampleCurrent      = 10.0;
    private int    _pulseWidth         = 150;
    private string _selectedTap        = "OFF";
    private double _stabThreshold      = 0.05;
    private int    _stabCount          = 5;
    private int    _stabTimeout        = 2000;

    // Sweep
    private double _sweepVStart        = 0.0;
    private double _sweepVStop         = 60.0;
    private double _sweepVStep         = 10.0;
    private bool   _isSweepRunning;

    // Status
    private string _statusText         = "Disconnected";
    private string _logText            = string.Empty;

    private CancellationTokenSource? _sweepCts;

    public MainViewModel() : this(new MrmsScpiClient(new SerialProtocolClient())) { }

    public MainViewModel(MrmsScpiClient client)
    {
        _client = client;

        AvailablePorts    = new ObservableCollection<string>();
        AvailableBaudRates = new ObservableCollection<int> { 9600, 19200, 38400, 57600, 115200 };
        AvailableTaps     = new ObservableCollection<string>
            { "OFF", "2S", "4S", "6S", "8S", "10S", "12S", "14S", "16S" };
        SweepData         = new ObservableCollection<SweepRowViewModel>();

        RefreshPortsCommand   = new RelayCommand(RefreshPorts);
        ConnectCommand        = new AsyncRelayCommand(ToggleConnectionAsync, () => !IsSweepRunning);
        ApplyConfigCommand    = new AsyncRelayCommand(ApplyConfigAsync,   () => IsConnected && !IsSweepRunning);
        AutoPickTapCommand    = new AsyncRelayCommand(AutoPickTapAsync,   () => IsConnected && !IsSweepRunning);
        MeasureFieldCommand   = new AsyncRelayCommand(MeasureFieldAsync,  () => IsConnected && !IsSweepRunning);
        MeasureResistCommand  = new AsyncRelayCommand(MeasureResistAsync, () => IsConnected && !IsSweepRunning);
        StartSweepCommand     = new AsyncRelayCommand(StartSweepAsync,    () => IsConnected && !IsSweepRunning);
        AbortSweepCommand     = new AsyncRelayCommand(AbortSweepAsync,    () => IsConnected && IsSweepRunning);
        ResetCommand          = new AsyncRelayCommand(ResetAsync,         () => IsConnected && !IsSweepRunning);
        ClearLogCommand       = new RelayCommand(() => LogText = string.Empty);
        ClearDataCommand      = new RelayCommand(() => SweepData.Clear());

        RefreshPorts();
    }

    // ── Collections ───────────────────────────────────────────────────────────

    public ObservableCollection<string> AvailablePorts    { get; }
    public ObservableCollection<int>    AvailableBaudRates { get; }
    public ObservableCollection<string> AvailableTaps     { get; }
    public ObservableCollection<SweepRowViewModel> SweepData { get; }

    // ── Connection properties ─────────────────────────────────────────────────

    public string? SelectedPort
    {
        get => _selectedPort;
        set { if (SetProperty(ref _selectedPort, value)) UpdateCommandStates(); }
    }

    public int SelectedBaudRate
    {
        get => _selectedBaudRate;
        set => SetProperty(ref _selectedBaudRate, value);
    }

    public bool IsConnected
    {
        get => _isConnected;
        private set
        {
            if (SetProperty(ref _isConnected, value))
            {
                OnPropertyChanged(nameof(ConnectionButtonText));
                UpdateCommandStates();
            }
        }
    }

    public string ConnectionButtonText => IsConnected ? "Disconnect" : "Connect";

    public string InstrumentId
    {
        get => _instrumentId;
        private set => SetProperty(ref _instrumentId, value);
    }

    // ── Configuration properties ──────────────────────────────────────────────

    public double MagnetVoltage
    {
        get => _magnetVoltage;
        set => SetProperty(ref _magnetVoltage, value);
    }

    public double SampleCurrent
    {
        get => _sampleCurrent;
        set => SetProperty(ref _sampleCurrent, value);
    }

    public int PulseWidth
    {
        get => _pulseWidth;
        set => SetProperty(ref _pulseWidth, value);
    }

    public string SelectedTap
    {
        get => _selectedTap;
        set => SetProperty(ref _selectedTap, value);
    }

    public double StabThreshold
    {
        get => _stabThreshold;
        set => SetProperty(ref _stabThreshold, value);
    }

    public int StabCount
    {
        get => _stabCount;
        set => SetProperty(ref _stabCount, value);
    }

    public int StabTimeout
    {
        get => _stabTimeout;
        set => SetProperty(ref _stabTimeout, value);
    }

    // ── Sweep properties ──────────────────────────────────────────────────────

    public double SweepVStart
    {
        get => _sweepVStart;
        set => SetProperty(ref _sweepVStart, value);
    }

    public double SweepVStop
    {
        get => _sweepVStop;
        set => SetProperty(ref _sweepVStop, value);
    }

    public double SweepVStep
    {
        get => _sweepVStep;
        set => SetProperty(ref _sweepVStep, value);
    }

    public bool IsSweepRunning
    {
        get => _isSweepRunning;
        private set
        {
            if (SetProperty(ref _isSweepRunning, value))
                UpdateCommandStates();
        }
    }

    // ── Status & log ──────────────────────────────────────────────────────────

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public string LogText
    {
        get => _logText;
        private set => SetProperty(ref _logText, value);
    }

    // ── Commands ──────────────────────────────────────────────────────────────

    public ICommand RefreshPortsCommand  { get; }
    public ICommand ConnectCommand       { get; }
    public ICommand ApplyConfigCommand   { get; }
    public ICommand AutoPickTapCommand   { get; }
    public ICommand MeasureFieldCommand  { get; }
    public ICommand MeasureResistCommand { get; }
    public ICommand StartSweepCommand    { get; }
    public ICommand AbortSweepCommand    { get; }
    public ICommand ResetCommand         { get; }
    public ICommand ClearLogCommand      { get; }
    public ICommand ClearDataCommand     { get; }

    // ── Cleanup ───────────────────────────────────────────────────────────────

    public async Task CleanupAsync()
    {
        _sweepCts?.Cancel();
        if (IsConnected)
        {
            await SafeDisconnectAsync();
        }
        _client.Dispose();
    }

    // ── Private: connection ───────────────────────────────────────────────────

    private void RefreshPorts()
    {
        var ports = SerialPort.GetPortNames()
                              .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
                              .ToArray();
        AvailablePorts.Clear();
        foreach (var p in ports) AvailablePorts.Add(p);

        if (SelectedPort is not null && !AvailablePorts.Contains(SelectedPort))
            SelectedPort = null;
        if (SelectedPort is null && AvailablePorts.Count > 0)
            SelectedPort = AvailablePorts[0];
    }

    private async Task ToggleConnectionAsync()
    {
        if (IsConnected) { await SafeDisconnectAsync(); return; }

        if (string.IsNullOrWhiteSpace(SelectedPort))
        {
            Log("No COM port selected.");
            return;
        }

        try
        {
            await _client.ConnectAsync(SelectedPort, SelectedBaudRate);
            IsConnected = true;
            Log($"Connected: {SelectedPort} @ {SelectedBaudRate}");

            var id = await _client.GetIdentificationAsync();
            InstrumentId = $"{id.Manufacturer},{id.Model},{id.SerialNumber},{id.FirmwareVersion}";
            Log($"Instrument: {InstrumentId}");
            StatusText = "Connected — idle";
        }
        catch (Exception ex)
        {
            Log($"Connect error: {ex.Message}");
            await SafeDisconnectAsync();
        }
    }

    private async Task SafeDisconnectAsync()
    {
        _sweepCts?.Cancel();
        IsSweepRunning = false;
        try { await _client.DisconnectAsync(); } catch (Exception ex) { Log($"Disconnect: {ex.Message}"); }
        IsConnected  = false;
        InstrumentId = "—";
        StatusText   = "Disconnected";
        Log("Disconnected.");
    }

    // ── Private: configuration ────────────────────────────────────────────────

    private async Task ApplyConfigAsync()
    {
        try
        {
            await _client.SetMagnetVoltageAsync(MagnetVoltage);
            await _client.SetSampleCurrentAsync(SampleCurrent);
            await _client.SetPulseWidthAsync(PulseWidth);
            await _client.SetTapAsync(SelectedTap);
            await _client.SetStabilityThresholdAsync(StabThreshold);
            await _client.SetStabilityCountAsync(StabCount);
            await _client.SetStabilityTimeoutAsync(StabTimeout);
            Log("Configuration applied.");
            StatusText = "Configuration applied";
        }
        catch (Exception ex) { Log($"ApplyConfig error: {ex.Message}"); }
    }

    private async Task AutoPickTapAsync()
    {
        try
        {
            StatusText = "Auto-picking tap…";
            var result = await _client.AutoPickTapAsync();
            SelectedTap = result.Tap;
            Log($"AutoPick → Tap={result.Tap}, R≈{result.RApproxOhm:F1} Ω, V_bat={result.VBatteryVolts:F1} V");
            StatusText = $"Tap selected: {result.Tap}";
        }
        catch (Exception ex) { Log($"AutoPick error: {ex.Message}"); StatusText = "AutoPick failed"; }
    }

    // ── Private: measurements ─────────────────────────────────────────────────

    private async Task MeasureFieldAsync()
    {
        try
        {
            double b = await _client.MeasureFieldAsync();
            Log($"Field = {b:F4} T");
            StatusText = $"Field: {b:F4} T";
        }
        catch (Exception ex) { Log($"MeasureField error: {ex.Message}"); }
    }

    private async Task MeasureResistAsync()
    {
        try
        {
            StatusText = "Measuring resistance…";
            var r = await _client.MeasureResistanceAsync();
            Log($"R = {r.ResistanceOhm:F2} Ω  (V+ = {r.VPlusVolts:F5} V, V− = {r.VMinusVolts:F5} V)");
            StatusText = $"R = {r.ResistanceOhm:F2} Ω";
        }
        catch (Exception ex) { Log($"MeasureResist error: {ex.Message}"); StatusText = "Measurement failed"; }
    }

    // ── Private: sweep ────────────────────────────────────────────────────────

    private async Task StartSweepAsync()
    {
        try
        {
            await _client.ConfigureSweepAsync(SweepVStart, SweepVStop, SweepVStep);
            SweepData.Clear();

            _sweepCts?.Dispose();
            _sweepCts  = new CancellationTokenSource();
            IsSweepRunning = true;
            StatusText = "Sweep running…";

            await _client.StartSweepAsync(
                point =>
                {
                    App.Current.Dispatcher.BeginInvoke(() =>
                    {
                        SweepData.Add(new SweepRowViewModel
                        {
                            Step        = point.Step,
                            VSetVolts   = point.VSetVolts,
                            BFieldTesla = point.BFieldTesla,
                            RSampleOhm  = point.RSampleOhm,
                            VPlusVolts  = point.VPlusVolts,
                            VMinusVolts = point.VMinusVolts,
                            Tap         = point.Tap,
                            Status      = point.Status,
                        });
                        Log($"DATA {point.Step}: V={point.VSetVolts:F1} V, B={point.BFieldTesla:F4} T, " +
                            $"R={point.RSampleOhm:F2} Ω, Tap={point.Tap}, {point.Status}");
                    });
                },
                _sweepCts.Token);

            Log("Sweep completed.");
            StatusText = "Sweep completed";
        }
        catch (OperationCanceledException)
        {
            Log("Sweep aborted.");
            StatusText = "Sweep aborted";
        }
        catch (Exception ex)
        {
            Log($"Sweep error: {ex.Message}");
            StatusText = "Sweep error";
        }
        finally
        {
            IsSweepRunning = false;
        }
    }

    private async Task AbortSweepAsync()
    {
        try
        {
            _sweepCts?.Cancel();
            await _client.AbortSweepAsync();
            Log("Abort command sent.");
        }
        catch (Exception ex) { Log($"Abort error: {ex.Message}"); }
    }

    private async Task ResetAsync()
    {
        try
        {
            await _client.ResetAsync();
            MagnetVoltage = 0.0;
            SelectedTap   = "OFF";
            Log("Device reset to defaults.");
            StatusText    = "Reset";
        }
        catch (Exception ex) { Log($"Reset error: {ex.Message}"); }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private void Log(string message)
    {
        var entry = $"{DateTime.Now:HH:mm:ss.fff}  {message}";
        LogText   = string.IsNullOrEmpty(LogText) ? entry : LogText + Environment.NewLine + entry;
    }

    private void UpdateCommandStates()
    {
        foreach (var cmd in new ICommand[]
            { ConnectCommand, ApplyConfigCommand, AutoPickTapCommand,
              MeasureFieldCommand, MeasureResistCommand,
              StartSweepCommand, AbortSweepCommand, ResetCommand })
        {
            if (cmd is AsyncRelayCommand arc)
                arc.RaiseCanExecuteChanged();
        }
    }
}

