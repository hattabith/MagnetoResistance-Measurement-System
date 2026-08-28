using System.IO.Ports;
using System.Threading.Channels;

namespace MRMS.App.Services;

public sealed class SerialProtocolClient : IDisposable
{
    private SerialPort? _serialPort;
    private CancellationTokenSource? _readerCts;
    private Task? _readerTask;
    private Channel<string> _lineChannel = Channel.CreateUnbounded<string>();
    private readonly SemaphoreSlim _commandLock = new(1, 1);

    public bool IsConnected => _serialPort is { IsOpen: true };

    public async Task ConnectAsync(string portName, int baudRate, CancellationToken cancellationToken = default)
    {
        if (IsConnected)
        {
            throw new InvalidOperationException("Serial port is already connected.");
        }

        _lineChannel = Channel.CreateUnbounded<string>();

        var serialPort = new SerialPort(portName, baudRate)
        {
            NewLine = "\n",
            ReadTimeout = 500,
            WriteTimeout = 500,
            DtrEnable = true,
            RtsEnable = false
        };

        await Task.Run(serialPort.Open, cancellationToken);

        _serialPort = serialPort;
        _readerCts = new CancellationTokenSource();
        _readerTask = Task.Run(() => ReaderLoop(_readerCts.Token), CancellationToken.None);
    }

    public async Task DisconnectAsync()
    {
        if (_serialPort is null)
        {
            return;
        }

        _readerCts?.Cancel();

        if (_readerTask is not null)
        {
            try
            {
                await _readerTask;
            }
            catch
            {
                // Reader loop can exit with cancellation or I/O errors during close.
            }
        }

        var serialPort = _serialPort;
        _serialPort = null;

        await Task.Run(() =>
        {
            if (serialPort.IsOpen)
            {
                serialPort.Close();
            }

            serialPort.Dispose();
        });

        _readerCts?.Dispose();
        _readerCts = null;
        _readerTask = null;
    }

    /// <summary>
    /// Send a SCPI query command and return the first non-empty response line.
    /// </summary>
    public async Task<string> SendQueryAsync(
        string command,
        CancellationToken cancellationToken = default,
        int timeoutMs = 2000)
    {
        if (!IsConnected || _serialPort is null)
        {
            throw new InvalidOperationException("Serial port is not connected.");
        }

        await _commandLock.WaitAsync(cancellationToken);
        try
        {
            DrainPendingLines();
            await Task.Run(() => _serialPort.WriteLine(command), cancellationToken);

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(timeoutMs);

            while (true)
            {
                var line = await _lineChannel.Reader.ReadAsync(timeoutCts.Token);
                if (!string.IsNullOrWhiteSpace(line))
                {
                    return line;
                }
            }
        }
        catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException($"Timeout waiting for response to '{command}'.", ex);
        }
        finally
        {
            _commandLock.Release();
        }
    }

    /// <summary>
    /// Send a SCPI write command that produces no response (fire-and-forget with
    /// a brief drain window to absorb any unexpected echo).
    /// </summary>
    public async Task SendWriteAsync(
        string command,
        CancellationToken cancellationToken = default)
    {
        if (!IsConnected || _serialPort is null)
        {
            throw new InvalidOperationException("Serial port is not connected.");
        }

        await _commandLock.WaitAsync(cancellationToken);
        try
        {
            DrainPendingLines();
            await Task.Run(() => _serialPort.WriteLine(command), cancellationToken);
            // Short settling delay — absorbs any unsolicited startup text.
            await Task.Delay(50, cancellationToken);
            DrainPendingLines();
        }
        finally
        {
            _commandLock.Release();
        }
    }

    /// <summary>
    /// Send a SCPI command that returns a single response line (e.g. AUTOP, ABOR).
    /// </summary>
    public Task<string> SendCommandAsync(
        string command,
        CancellationToken cancellationToken = default,
        int timeoutMs = 3000)
        => SendQueryAsync(command, cancellationToken, timeoutMs);

    /// <summary>
    /// Send SWEE:INIT and stream DATA lines to <paramref name="onLine"/> until
    /// SWEEP:COMPLETED or SWEEP:ABORTED is received.
    /// </summary>
    public async Task SendSweepAsync(
        Action<string> onLine,
        CancellationToken cancellationToken = default,
        int overallTimeoutMs = 600_000)
    {
        if (!IsConnected || _serialPort is null)
        {
            throw new InvalidOperationException("Serial port is not connected.");
        }

        await _commandLock.WaitAsync(cancellationToken);
        try
        {
            DrainPendingLines();
            await Task.Run(() => _serialPort.WriteLine("SWEE:INIT"), cancellationToken);

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(overallTimeoutMs);

            while (true)
            {
                var line = await _lineChannel.Reader.ReadAsync(timeoutCts.Token);
                line = line.Trim();
                if (string.IsNullOrEmpty(line)) continue;

                onLine(line);

                if (line.Equals("SWEEP:COMPLETED", StringComparison.OrdinalIgnoreCase) ||
                    line.Equals("SWEEP:ABORTED",   StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }
            }
        }
        catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException("Sweep timed out waiting for SWEEP:COMPLETED.", ex);
        }
        finally
        {
            _commandLock.Release();
        }
    }

    private void ReaderLoop(CancellationToken cancellationToken)
    {
        if (_serialPort is null)
        {
            return;
        }

        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var line = _serialPort.ReadLine().Trim();
                if (!string.IsNullOrEmpty(line))
                {
                    _lineChannel.Writer.TryWrite(line);
                }
            }
            catch (TimeoutException)
            {
                continue;
            }
            catch
            {
                break;
            }
        }
    }

    private void DrainPendingLines()
    {
        while (_lineChannel.Reader.TryRead(out _))
        {
        }
    }

    public void Dispose()
    {
        if (IsConnected)
        {
            DisconnectAsync().GetAwaiter().GetResult();
        }

        _commandLock.Dispose();
    }
}
