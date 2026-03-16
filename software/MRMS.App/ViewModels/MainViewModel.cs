using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO.Ports;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Input;
using MRMS.App.Commands;
using MRMS.App.Services;

namespace MRMS.App.ViewModels;

public sealed class MainViewModel : ObservableObject
{
    private static readonly Regex StatusRegex = new(
        @"OK\s+RELAY=(ON|OFF)\s+HALL_RAW=(-?\d+)\s+HALL_V=([-+]?\d*\.?\d+)",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    private readonly SerialProtocolClient _serialClient;

    private string? _selectedPort;
    private int _selectedBaudRate = 115200;
    private int _relayOnTimeSeconds = 5;
    private bool _isConnected;
    private bool _isRelayRunInProgress;
    private string _relayStatusText = "Relay: Unknown";
    private string _latestHallText = "Hall: no data";
    private string _sensorLogText = string.Empty;

    private CancellationTokenSource? _relayRunCts;

    public MainViewModel() : this(new SerialProtocolClient())
    {
    }

    public MainViewModel(SerialProtocolClient serialClient)
    {
        _serialClient = serialClient;

        AvailableBaudRates = new ObservableCollection<int> { 9600, 19200, 38400, 57600, 115200 };
        AvailableRelayTimes = new ObservableCollection<int> { 1, 2, 5, 10, 15, 30, 60 };
        AvailablePorts = new ObservableCollection<string>();

        RefreshPortsCommand = new RelayCommand(RefreshPorts);
        ConnectCommand = new AsyncRelayCommand(ToggleConnectionAsync, () => !IsRelayRunInProgress);
        StartRelayCommand = new AsyncRelayCommand(StartRelayAsync, () => IsConnected && !IsRelayRunInProgress);

        RefreshPorts();
    }

    public ObservableCollection<string> AvailablePorts { get; }

    public ObservableCollection<int> AvailableBaudRates { get; }

    public ObservableCollection<int> AvailableRelayTimes { get; }

    public string? SelectedPort
    {
        get => _selectedPort;
        set
        {
            if (SetProperty(ref _selectedPort, value))
            {
                UpdateCommandStates();
            }
        }
    }

    public int SelectedBaudRate
    {
        get => _selectedBaudRate;
        set => SetProperty(ref _selectedBaudRate, value);
    }

    public int RelayOnTimeSeconds
    {
        get => _relayOnTimeSeconds;
        set => SetProperty(ref _relayOnTimeSeconds, value);
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

    public bool IsRelayRunInProgress
    {
        get => _isRelayRunInProgress;
        private set
        {
            if (SetProperty(ref _isRelayRunInProgress, value))
            {
                UpdateCommandStates();
            }
        }
    }

    public string ConnectionButtonText => IsConnected ? "Disconnect" : "Connect";

    public string RelayStatusText
    {
        get => _relayStatusText;
        private set => SetProperty(ref _relayStatusText, value);
    }

    public string LatestHallText
    {
        get => _latestHallText;
        private set => SetProperty(ref _latestHallText, value);
    }

    public string SensorLogText
    {
        get => _sensorLogText;
        private set => SetProperty(ref _sensorLogText, value);
    }

    public ICommand RefreshPortsCommand { get; }

    public ICommand ConnectCommand { get; }

    public ICommand StartRelayCommand { get; }

    public async Task CleanupAsync()
    {
        _relayRunCts?.Cancel();

        if (IsConnected)
        {
            await SafeDisconnectAsync();
        }

        _serialClient.Dispose();
    }

    private void RefreshPorts()
    {
        var ports = SerialPort.GetPortNames().OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToArray();

        AvailablePorts.Clear();
        foreach (var port in ports)
        {
            AvailablePorts.Add(port);
        }

        if (!string.IsNullOrWhiteSpace(SelectedPort) && !AvailablePorts.Contains(SelectedPort))
        {
            SelectedPort = null;
        }

        if (SelectedPort is null && AvailablePorts.Count > 0)
        {
            SelectedPort = AvailablePorts[0];
        }
    }

    private async Task ToggleConnectionAsync()
    {
        if (IsConnected)
        {
            await SafeDisconnectAsync();
            return;
        }

        if (string.IsNullOrWhiteSpace(SelectedPort))
        {
            AppendLog("No COM port selected.");
            return;
        }

        try
        {
            await _serialClient.ConnectAsync(SelectedPort, SelectedBaudRate);
            IsConnected = true;
            AppendLog($"Connected: {SelectedPort} @ {SelectedBaudRate}");

            var response = await _serialClient.SendCommandAsync("STATUS");
            ApplyStatusResponse(response);
        }
        catch (Exception ex)
        {
            AppendLog($"Connect error: {ex.Message}");
            await SafeDisconnectAsync();
        }
    }

    private async Task SafeDisconnectAsync()
    {
        _relayRunCts?.Cancel();
        IsRelayRunInProgress = false;

        try
        {
            await _serialClient.DisconnectAsync();
        }
        catch (Exception ex)
        {
            AppendLog($"Disconnect warning: {ex.Message}");
        }

        IsConnected = false;
        RelayStatusText = "Relay: Disconnected";
        LatestHallText = "Hall: no data";
        AppendLog("Disconnected");
    }

    private async Task StartRelayAsync()
    {
        if (!IsConnected)
        {
            return;
        }

        if (RelayOnTimeSeconds <= 0)
        {
            AppendLog("Relay ON time must be greater than 0 seconds.");
            return;
        }

        _relayRunCts?.Cancel();
        _relayRunCts = new CancellationTokenSource();

        IsRelayRunInProgress = true;

        try
        {
            var token = _relayRunCts.Token;
            var duration = TimeSpan.FromSeconds(RelayOnTimeSeconds);
            var stopWatch = Stopwatch.StartNew();

            var onResponse = await _serialClient.SendCommandAsync("RELAY ON", token);
            ApplyStatusResponse(onResponse);

            while (stopWatch.Elapsed < duration && !token.IsCancellationRequested)
            {
                var response = await _serialClient.SendCommandAsync("STATUS", token);
                ApplyStatusResponse(response);
                await Task.Delay(200, token);
            }
        }
        catch (OperationCanceledException)
        {
            AppendLog("Relay run canceled.");
        }
        catch (Exception ex)
        {
            AppendLog($"Relay run error: {ex.Message}");
        }
        finally
        {
            try
            {
                if (IsConnected)
                {
                    var offResponse = await _serialClient.SendCommandAsync("RELAY OFF");
                    ApplyStatusResponse(offResponse);
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Failed to turn relay off: {ex.Message}");
            }

            IsRelayRunInProgress = false;
        }
    }

    private void ApplyStatusResponse(string responseLine)
    {
        var match = StatusRegex.Match(responseLine);
        if (!match.Success)
        {
            AppendLog(responseLine);
            return;
        }

        var relayState = match.Groups[1].Value.ToUpperInvariant();
        var hallRaw = match.Groups[2].Value;
        var hallVoltage = match.Groups[3].Value;

        RelayStatusText = relayState == "ON" ? "Relay: ON" : "Relay: OFF";
        LatestHallText = $"Hall RAW: {hallRaw}, Voltage: {hallVoltage} V";

        var logLine = $"{DateTime.Now:HH:mm:ss.fff} | Relay={relayState}, HallRaw={hallRaw}, HallV={hallVoltage} V";
        AppendLog(logLine);
    }

    private void AppendLog(string line)
    {
        if (string.IsNullOrWhiteSpace(SensorLogText))
        {
            SensorLogText = line;
            return;
        }

        SensorLogText += Environment.NewLine + line;
    }

    private void UpdateCommandStates()
    {
        if (ConnectCommand is AsyncRelayCommand connect)
        {
            connect.RaiseCanExecuteChanged();
        }

        if (StartRelayCommand is AsyncRelayCommand start)
        {
            start.RaiseCanExecuteChanged();
        }
    }
}
