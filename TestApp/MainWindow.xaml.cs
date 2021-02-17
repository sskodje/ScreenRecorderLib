using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win32Interop.WinHandles;

namespace TestApp
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window, INotifyPropertyChanged
    {
        private Recorder _rec;
        private DispatcherTimer _progressTimer;
        private int _secondsElapsed;
        private Stream _outputStream;
        private bool _isRecording;

        public bool IsRecording
        {
            get { return _isRecording; }
            set
            {
                _isRecording = value;
            }
        }

        public int VideoBitrate { get; set; } = 7000;
        public int VideoFramerate { get; set; } = 60;
        public int VideoQuality { get; set; } = 70;
        public int SnapshotsIntervalInSec { get; set; } = 10;
        public bool IsAudioEnabled { get; set; } = true;
        public bool IsMousePointerEnabled { get; set; } = true;
        public bool IsFixedFramerate { get; set; } = false;
        public bool IsThrottlingDisabled { get; set; } = false;
        public bool IsHardwareEncodingEnabled { get; set; } = true;
        public bool IsLowLatencyEnabled { get; set; } = false;
        public bool IsMp4FastStartEnabled { get; set; } = false;
        public bool IsFragmentedMp4Enabled { get; set; } = false;
        public bool IsMouseClicksDetected { get; set; } = false;
        public string MouseLeftClickColor { get; set; } = "#ffff00";
        public string MouseRightClickColor { get; set; } = "#006aff";
        public int MouseClickRadius { get; set; } = 20;
        public int MouseClickDuration { get; set; } = 50;
        public Dictionary<string, string> AudioInputsList { get; set; } = new Dictionary<string, string>();
        public Dictionary<string, string> AudioOutputsList { get; set; } = new Dictionary<string, string>();
        public bool IsAudioInEnabled { get; set; } = false;
        public bool IsAudioOutEnabled { get; set; } = true;
        public string LogFilePath { get; set; } = "log.txt";
        public bool IsLogToFileEnabled { get; set; }
        public bool IsLogEnabled { get; set; } = true;
        public DisplayOutput SelectedDisplayOutput { get; set; }

        public LogLevel LogSeverityLevel { get; set; } = LogLevel.Debug;

        private bool _recordToStream;
        public bool RecordToStream
        {
            get { return _recordToStream; }
            set
            {
                if (_recordToStream != value)
                {
                    _recordToStream = value;
                    RaisePropertyChanged("RecordToStream");
                    if (value)
                    {
                        CurrentRecordingMode = RecorderMode.Video;
                        this.RecordingModeComboBox.IsEnabled = false;
                    }
                    else
                    {
                        this.RecordingModeComboBox.IsEnabled = true;
                    }
                }
            }
        }

        private bool _isExcludeWindowEnabled = false;
        public bool IsExcludeWindowEnabled
        {
            get { return _isExcludeWindowEnabled; }
            set
            {
                if (_isExcludeWindowEnabled != value)
                {
                    _isExcludeWindowEnabled = value;
                    RaisePropertyChanged("IsExcludeWindowEnabled");

                    IntPtr hwnd = new WindowInteropHelper(this).Handle;
                    Recorder.SetExcludeFromCapture(hwnd, value);
                }
            }
        }
        private bool _snapshotsWithVideo = false;
        public bool SnapshotsWithVideo
        {
            get { return _snapshotsWithVideo; }
            set
            {
                if (_snapshotsWithVideo != value)
                {
                    _snapshotsWithVideo = value;
                    RaisePropertyChanged("SnapshotsWithVideo");
                    if (value)
                    {
                        this.SnapshotImageFormatPanel.Visibility = Visibility.Visible;
                        this.SnapshotsIntervalPanel.Visibility = Visibility.Visible;
                    }
                    else
                    {
                        this.SnapshotImageFormatPanel.Visibility = Visibility.Collapsed;
                        this.SnapshotsIntervalPanel.Visibility = Visibility.Collapsed;
                    }
                }
            }
        }

        private RecorderMode _currentRecordingMode;
        public RecorderMode CurrentRecordingMode
        {
            get { return _currentRecordingMode; }
            set
            {
                if (_currentRecordingMode != value)
                {
                    _currentRecordingMode = value;
                    RaisePropertyChanged("CurrentRecordingMode");
                }
            }
        }

        private RecorderApi _currentRecordingApi;
        public RecorderApi CurrentRecordingApi
        {
            get { return _currentRecordingApi; }
            set
            {
                if (_currentRecordingApi != value)
                {
                    _currentRecordingApi = value;
                    RaisePropertyChanged("CurrentRecordingApi");
                }
            }
        }

        private BitrateControlMode _currentVideoBitrateMode = BitrateControlMode.Quality;
        public BitrateControlMode CurrentVideoBitrateMode
        {
            get { return _currentVideoBitrateMode; }
            set
            {
                if (_currentVideoBitrateMode != value)
                {
                    _currentVideoBitrateMode = value;
                    RaisePropertyChanged("CurrentVideoBitrateMode");
                }
            }
        }

        private ImageFormat _currentImageFormat;
        public ImageFormat CurrentImageFormat
        {
            get { return _currentImageFormat; }
            set
            {
                if (_currentImageFormat != value)
                {
                    _currentImageFormat = value;
                    RaisePropertyChanged("CurrentImageFormat");
                }
            }
        }

        private MouseDetectionMode _currentMouseDetectionMode;
        public MouseDetectionMode CurrentMouseDetectionMode
        {
            get { return _currentMouseDetectionMode; }
            set
            {
                if (_currentMouseDetectionMode != value)
                {
                    _currentMouseDetectionMode = value;
                    RaisePropertyChanged("CurrentMouseDetectionMode");
                }
            }
        }


        public H264Profile CurrentH264Profile { get; set; } = H264Profile.Main;


        public MainWindow()
        {
            InitializeComponent();

            RefreshCaptureTargetItems();
        }

        private bool IsValidWindow(WindowHandle window)
        {
            return window.IsVisible() && window.IsValid && !String.IsNullOrEmpty(window.GetWindowText());
        }

        protected void RaisePropertyChanged(string propertyName)
        {
            PropertyChangedEventHandler handler = PropertyChanged;
            if (handler != null) handler(this, new PropertyChangedEventArgs(propertyName));
        }
        public event PropertyChangedEventHandler PropertyChanged;

        private void RecordButton_Click(object sender, RoutedEventArgs e)
        {
            if (IsRecording)
            {
                _rec.Stop();
                _progressTimer?.Stop();
                _progressTimer = null;
                _secondsElapsed = 0;
                RecordButton.IsEnabled = false;
                return;
            }
            OutputResultTextBlock.Text = "";
            UpdateProgress();
            string videoPath = "";
            if (CurrentRecordingMode == RecorderMode.Video)
            {
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + ".mp4");
            }
            else if (CurrentRecordingMode == RecorderMode.Slideshow)
            {
                //For slideshow just give a folder path as input.
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp) + "\\";
            }
            else if (CurrentRecordingMode == RecorderMode.Snapshot)
            {
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss-ff");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + GetImageExtension());
            }
            _progressTimer = new DispatcherTimer();
            _progressTimer.Tick += _progressTimer_Tick;
            _progressTimer.Interval = TimeSpan.FromSeconds(1);
            _progressTimer.Start();

            int right = 0;
            Int32.TryParse(this.RecordingAreaRightTextBox.Text, out right);
            int bottom = 0;
            Int32.TryParse(this.RecordingAreaBottomTextBox.Text, out bottom);
            int left = 0;
            Int32.TryParse(this.RecordingAreaLeftTextBox.Text, out left);
            int top = 0;
            Int32.TryParse(this.RecordingAreaTopTextBox.Text, out top);

            var selectedDisplay = this.ScreenComboBox.SelectedItem as DisplayOutput;

            IntPtr selectedWindowHandle = this.WindowComboBox.Items.Count > 0 ? ((RecordableWindow)this.WindowComboBox.SelectedItem).Handle : IntPtr.Zero;

            string audioOutputDevice = AudioOutputsComboBox.SelectedValue as string;
            string audioInputDevice = AudioInputsComboBox.SelectedValue as string;
            List<string> displayDevicesToRecord = this.ScreenComboBox.Items.Cast<DisplayOutput>().Where(x => x.IsSelected).Select(x => x.DeviceName).ToList();
            if (displayDevicesToRecord.Count == 0 && this.ScreenComboBox.SelectedItem != null)
            {
                displayDevicesToRecord.Add(((DisplayOutput)this.ScreenComboBox.SelectedItem).DeviceName);
            }

            RecorderOptions options = new RecorderOptions
            {
                RecorderMode = CurrentRecordingMode,
                RecorderApi = CurrentRecordingApi,
                IsThrottlingDisabled = this.IsThrottlingDisabled,
                IsHardwareEncodingEnabled = this.IsHardwareEncodingEnabled,
                IsLowLatencyEnabled = this.IsLowLatencyEnabled,
                IsMp4FastStartEnabled = this.IsMp4FastStartEnabled,
                IsFragmentedMp4Enabled = this.IsFragmentedMp4Enabled,
                IsLogEnabled = this.IsLogEnabled,
                LogSeverityLevel = this.LogSeverityLevel,
                LogFilePath = IsLogToFileEnabled ? this.LogFilePath : "",

                AudioOptions = new AudioOptions
                {
                    Bitrate = AudioBitrate.bitrate_96kbps,
                    Channels = AudioChannels.Stereo,
                    IsAudioEnabled = this.IsAudioEnabled,
                    IsOutputDeviceEnabled = IsAudioOutEnabled,
                    IsInputDeviceEnabled = IsAudioInEnabled,
                    AudioOutputDevice = audioOutputDevice,
                    AudioInputDevice = audioInputDevice,
                    InputVolume = (float)InputVolumeSlider.Value,
                    OutputVolume = (float)OutputVolumeSlider.Value
                },
                VideoOptions = new VideoOptions
                {
                    BitrateMode = this.CurrentVideoBitrateMode,
                    Bitrate = VideoBitrate * 1000,
                    Framerate = this.VideoFramerate,
                    Quality = this.VideoQuality,
                    IsFixedFramerate = this.IsFixedFramerate,
                    EncoderProfile = this.CurrentH264Profile,
                    SnapshotFormat = CurrentImageFormat,
                    SnapshotsWithVideo = this.SnapshotsWithVideo,
                    SnapshotsInterval = this.SnapshotsIntervalInSec
                },
                DisplayOptions = new DisplayOptions
                {
                    DisplayDevices = displayDevicesToRecord,
                    WindowHandle = selectedWindowHandle,
                    Left = left,
                    Top = top,
                    Right = right,
                    Bottom = bottom
                },
                MouseOptions = new MouseOptions
                {
                    IsMouseClicksDetected = this.IsMouseClicksDetected,
                    IsMousePointerEnabled = this.IsMousePointerEnabled,
                    MouseClickDetectionColor = this.MouseLeftClickColor,
                    MouseRightClickDetectionColor = this.MouseRightClickColor,
                    MouseClickDetectionRadius = this.MouseClickRadius,
                    MouseClickDetectionDuration = this.MouseClickDuration,
                    MouseClickDetectionMode = this.CurrentMouseDetectionMode
                }
            };

            if (_rec == null)
            {
                _rec = Recorder.CreateRecorder(options);
                _rec.OnRecordingComplete += Rec_OnRecordingComplete;
                _rec.OnRecordingFailed += Rec_OnRecordingFailed;
                _rec.OnStatusChanged += _rec_OnStatusChanged;
                _rec.OnSnapshotSaved += _rec_OnSnapshotSaved;
            }
            else
            {
                _rec.SetOptions(options);
            }
            if (RecordToStream)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(videoPath));
                _outputStream = new FileStream(videoPath, FileMode.Create);
                _rec.Record(_outputStream);
            }
            else
            {
                _rec.Record(videoPath);
            }
            _secondsElapsed = 0;
            IsRecording = true;
        }

        private string GetImageExtension()
        {
            switch (CurrentImageFormat)
            {
                case ImageFormat.JPEG:
                    return ".jpg";
                case ImageFormat.TIFF:
                    return ".tiff";
                case ImageFormat.BMP:
                    return ".bmp";
                default:
                case ImageFormat.PNG:
                    return ".png";
            }
        }

        private void Rec_OnRecordingFailed(object sender, RecordingFailedEventArgs e)
        {
            Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Normal, (Action)(() =>
            {
                PauseButton.Visibility = Visibility.Collapsed;
                RecordButton.Content = "Record";
                RecordButton.IsEnabled = true;
                StatusTextBlock.Text = "Error:";
                ErrorTextBlock.Visibility = Visibility.Visible;
                ErrorTextBlock.Text = e.Error;
                IsRecording = false;
                CleanupResources();
            }));
        }
        private void Rec_OnRecordingComplete(object sender, RecordingCompleteEventArgs e)
        {
            Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Normal, (Action)(() =>
            {
                string filePath = e.FilePath;
                if (RecordToStream)
                {
                    filePath = ((FileStream)_outputStream)?.Name;
                }

                OutputResultTextBlock.Text = filePath;
                PauseButton.Visibility = Visibility.Collapsed;
                RecordButton.Content = "Record";
                RecordButton.IsEnabled = true;
                this.StatusTextBlock.Text = "Completed";
                IsRecording = false;
                CleanupResources();
            }));
        }
        private void _rec_OnSnapshotSaved(object sender, SnapshotSavedEventArgs e)
        {
            string filepath = e.SnapshotPath;
        }
        private void CleanupResources()
        {
            _outputStream?.Flush();
            _outputStream?.Dispose();
            _outputStream = null;

            _progressTimer?.Stop();
            _progressTimer = null;
            _secondsElapsed = 0;

            _rec?.Dispose();
            _rec = null;
        }

        private void _rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
        {
            Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Normal, (Action)(() =>
            {
                ErrorTextBlock.Visibility = Visibility.Collapsed;
                switch (e.Status)
                {
                    case RecorderStatus.Idle:
                        this.StatusTextBlock.Text = "Idle";
                        this.SettingsPanel.IsEnabled = true;
                        break;
                    case RecorderStatus.Recording:
                        PauseButton.Visibility = Visibility.Visible;
                        if (_progressTimer != null)
                            _progressTimer.IsEnabled = true;
                        RecordButton.Content = "Stop";
                        PauseButton.Content = "Pause";
                        this.StatusTextBlock.Text = "Recording";
                        this.SettingsPanel.IsEnabled = false;
                        break;
                    case RecorderStatus.Paused:
                        if (_progressTimer != null)
                            _progressTimer.IsEnabled = false;
                        PauseButton.Content = "Resume";
                        this.StatusTextBlock.Text = "Paused";
                        break;
                    case RecorderStatus.Finishing:
                        PauseButton.Visibility = Visibility.Collapsed;
                        this.StatusTextBlock.Text = "Finalizing video";
                        break;
                    default:
                        break;
                }
            }));
        }

        private void _progressTimer_Tick(object sender, EventArgs e)
        {
            _secondsElapsed++;
            UpdateProgress();
        }
        private void UpdateProgress()
        {
            TimeStampTextBlock.Text = TimeSpan.FromSeconds(_secondsElapsed).ToString();
        }
        private void PauseButton_Click(object sender, RoutedEventArgs e)
        {
            if (_rec.Status == RecorderStatus.Paused)
            {
                _rec.Resume();
                return;
            }
            _rec.Pause();
        }

        private void ScreenComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (e.AddedItems.Count == 0)
                return;
            SetComboBoxTitle();
            if (this.WindowComboBox.SelectedIndex == 0)
            {
                SetScreenRect();
            }
        }

        private void SetScreenRect()
        {
            var checkedScreens = this.ScreenComboBox.Items.Cast<DisplayOutput>().Where(x => x.IsSelected).ToList();
            if (checkedScreens.Count > 0)
            {
                this.RecordingAreaRightTextBox.Text = checkedScreens.Max(x => x.Position.X + x.Width).ToString();
                this.RecordingAreaBottomTextBox.Text = checkedScreens.Max(x => x.Position.Y + x.Height).ToString();
                this.RecordingAreaLeftTextBox.Text = 0.ToString();
                this.RecordingAreaTopTextBox.Text = 0.ToString();
            }
            else if (this.ScreenComboBox.SelectedItem != null)
            {
                var screen = this.ScreenComboBox.SelectedItem as DisplayOutput;
                this.RecordingAreaRightTextBox.Text = screen.Width.ToString();
                this.RecordingAreaBottomTextBox.Text = screen.Height.ToString();
                this.RecordingAreaLeftTextBox.Text = 0.ToString();
                this.RecordingAreaTopTextBox.Text = 0.ToString();
            }
            else
            {
                this.RecordingAreaRightTextBox.Text = 0.ToString();
                this.RecordingAreaBottomTextBox.Text = 0.ToString();
                this.RecordingAreaLeftTextBox.Text = 0.ToString();
                this.RecordingAreaTopTextBox.Text = 0.ToString();
            }
        }

        private void Hyperlink_Click(object sender, RoutedEventArgs e)
        {
            Process.Start(this.OutputResultTextBlock.Text);
        }

        private void TextBox_GotFocus(object sender, RoutedEventArgs e)
        {
            ((TextBox)sender).SelectAll();
        }

        private void DeleteTempFilesButton_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Directory.Delete(Path.Combine(Path.GetTempPath(), "ScreenRecorder"), true);
                MessageBox.Show("Temp files deleted");
            }
            catch (Exception ex)
            {
                MessageBox.Show("An error occured while deleting files: " + ex.Message);
            }
        }
        private void OpenRecordedFilesFolderButton_Click(object sender, RoutedEventArgs e)
        {
            string directory = Path.Combine(Path.GetTempPath(), "ScreenRecorder");
            if (!Directory.Exists(directory))
            {
                Directory.CreateDirectory(directory);
            }
            Process.Start(directory);
        }
        private void RecordingBitrateModeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (VideoQualityPanel != null)
            {
                VideoQualityPanel.Visibility = CurrentVideoBitrateMode == BitrateControlMode.Quality ? Visibility.Visible : Visibility.Collapsed;
                VideoBitratePanel.Visibility = CurrentVideoBitrateMode == BitrateControlMode.Quality ? Visibility.Collapsed : Visibility.Visible;
            }
        }

        private void ColorTextBox_TextChanged(object sender, TextChangedEventArgs e)
        {
            try
            {
                ((TextBox)sender).BorderBrush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(((TextBox)sender).Text));
            }
            catch
            {
                ((TextBox)sender).ClearValue(TextBox.BorderBrushProperty);
            }
        }

        private void RecordingModeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (this.AudioInPanel != null)
            {
                switch (CurrentRecordingMode)
                {
                    case RecorderMode.Video:
                        this.VideoBitrateModePanel.Visibility = Visibility.Visible;
                        this.VideoProfilePanel.Visibility = Visibility.Visible;
                        this.SnapshotImageFormatPanel.Visibility = SnapshotsWithVideo ? Visibility.Visible : Visibility.Collapsed;
                        this.SnapshotsIntervalPanel.Visibility = SnapshotsWithVideo ? Visibility.Visible : Visibility.Collapsed;
                        break;
                    case RecorderMode.Slideshow:
                    case RecorderMode.Snapshot:
                        this.VideoBitrateModePanel.Visibility = Visibility.Collapsed;
                        this.VideoProfilePanel.Visibility = Visibility.Collapsed;
                        this.SnapshotImageFormatPanel.Visibility = Visibility.Visible;
                        this.SnapshotsIntervalPanel.Visibility = Visibility.Collapsed;
                        break;
                    default:
                        break;
                }
            }
        }

        private void OnInputVolumeChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (_rec != null)
            {
                _rec.SetInputVolume((float)e.NewValue);
            }

        }

        private void OnOutputVolumeChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (_rec != null)
            {
                _rec.SetOutputVolume((float)e.NewValue);
            }
        }

        private void RecordingApiComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (CurrentRecordingApi == RecorderApi.DesktopDuplication)
            {
                this.WindowComboBox.Visibility = Visibility.Collapsed;
                this.CoordinatesPanel.IsEnabled = true;
                SetScreenRect();
            }
            else if (CurrentRecordingApi == RecorderApi.WindowsGraphicsCapture)
            {
                this.WindowComboBox.Visibility = Visibility.Visible;
                this.CoordinatesPanel.IsEnabled = false;
            }
        }

        private void WindowComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (e.AddedItems.Count == 0)
                return;

            var window = this.WindowComboBox.Items[this.WindowComboBox.SelectedIndex] as RecordableWindow;
            if (window.Handle == IntPtr.Zero)
            {
                SetScreenRect();
            }
            else
            {
                NativeMethods.RECT windowRect;
                if (NativeMethods.GetWindowRect(window.Handle, out windowRect))
                {
                    this.RecordingAreaRightTextBox.Text = windowRect.Right.ToString();
                    this.RecordingAreaBottomTextBox.Text = windowRect.Bottom.ToString();
                    this.RecordingAreaLeftTextBox.Text = windowRect.Left.ToString();
                    this.RecordingAreaTopTextBox.Text = windowRect.Top.ToString();
                }
            }
        }
        private void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            RefreshCaptureTargetItems();
        }
        private void RefreshCaptureTargetItems()
        {
            RefreshScreenComboBox();
            RefreshWindowComboBox();
            RefreshAudioComboBoxes();
        }
        private void RefreshScreenComboBox()
        {
            this.ScreenComboBox.Items.Clear();
            this.ScreenComboBox.Items.Add(new DisplayOutput
            {
                DeviceName = null,
                DisplayName = "All monitors",
                IsCheckable = false,
                Width = 0,
                Height = 0
            });

            foreach (var target in Recorder.GetDisplays())
            {
                this.ScreenComboBox.Items.Add(new DisplayOutput
                {
                    DeviceName = target.DeviceName,
                    DisplayName = target.MonitorName,
                    Width = target.Width,
                    Height = target.Height,
                    Position = new System.Drawing.Point(target.PosX, target.PosY)
                });
            }

            ScreenComboBox.SelectedIndex = 0;
        }
        private void RefreshWindowComboBox()
        {
            this.WindowComboBox.Items.Clear();
            this.WindowComboBox.Items.Add(new RecordableWindow("-- No window selected --", IntPtr.Zero));

            foreach (RecordableWindow window in Recorder.GetWindows())
            {
                this.WindowComboBox.Items.Add(window);
            }

            WindowComboBox.SelectedIndex = 0;
        }
        private void RefreshAudioComboBoxes()
        {
            AudioOutputsList.Clear();
            AudioInputsList.Clear();

            AudioOutputsList.Add("", "Default playback device");
            AudioInputsList.Add("", "Default recording device");
            foreach (var kvp in Recorder.GetSystemAudioDevices(AudioDeviceSource.OutputDevices))
            {
                AudioOutputsList.Add(kvp.Key, kvp.Value);
            }
            foreach (var kvp in Recorder.GetSystemAudioDevices(AudioDeviceSource.InputDevices))
            {
                AudioInputsList.Add(kvp.Key, kvp.Value);
            }

            // Since Dictionary is not "observable", reset the reference.
            AudioOutputsComboBox.ItemsSource = null;
            AudioInputsComboBox.ItemsSource = null;

            AudioOutputsComboBox.ItemsSource = AudioOutputsList;
            AudioInputsComboBox.ItemsSource = AudioInputsList;

            AudioOutputsComboBox.SelectedIndex = 0;
            AudioInputsComboBox.SelectedIndex = 0;
        }

        private void CheckBox_CheckedChanged(object sender, RoutedEventArgs e)
        {
            SetComboBoxTitle();
            SetScreenRect();
        }
        private void SetComboBoxTitle()
        {
            this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
            {
                if (this.ScreenComboBox.Items.Cast<DisplayOutput>().Any(x => x.IsSelected))
                {
                    var displays = this.ScreenComboBox.Items.Cast<DisplayOutput>().Where(x => x.IsSelected).Select(x => $"{x.DisplayName} ({x.DeviceName})");
                    this.ScreenComboBox.Text = string.Join(", ", displays);
                }
            }));
        }
    }

    internal class NativeMethods
    {
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left;        // x position of upper-left corner
            public int Top;         // y position of upper-left corner
            public int Right;       // x position of lower-right corner
            public int Bottom;      // y position of lower-right corner
        }
    }
}
