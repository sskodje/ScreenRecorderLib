using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
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

        public List<OverlayModel> Overlays { get; } = new List<OverlayModel>();
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
        public ObservableCollection<MediaDevice> VideoCaptureDevices { get; set; } = new ObservableCollection<MediaDevice>();
        public bool IsAudioInEnabled { get; set; } = false;
        public bool IsAudioOutEnabled { get; set; } = true;
        public string LogFilePath { get; set; } = "log.txt";
        public bool IsLogToFileEnabled { get; set; }
        public bool IsLogEnabled { get; set; } = true;

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

            InitializeDefaultOverlays();
            RefreshCaptureTargetItems();
        }

        private void InitializeDefaultOverlays()
        {
            Overlays.Clear();
            Overlays.Add(new OverlayModel
            {
                Overlay = new CameraCaptureOverlay
                {
                    AnchorPosition = Anchor.TopLeft,
                    Width = 0,
                    Height = 250,
                    OffsetX = 100,
                    OffsetY = 100
                },
                IsEnabled = true
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new VideoOverlay
                {
                    AnchorPosition = Anchor.TopRight,
                    FilePath = @"testmedia\cat.mp4",
                    Height = 200,
                    OffsetX = 50,
                    OffsetY = 50
                },
                IsEnabled = true
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new PictureOverlay
                {
                    AnchorPosition = Anchor.BottomLeft,
                    FilePath = @"testmedia\alphatest.png",
                    Height = 300,
                    OffsetX = 0,
                    OffsetY = 0
                },
                IsEnabled = true
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new PictureOverlay
                {
                    AnchorPosition = Anchor.BottomRight,
                    FilePath = @"testmedia\giftest.gif",
                    Height = 300,
                    OffsetX = 75,
                    OffsetY = 25
                },
                IsEnabled = true
            });
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
            _progressTimer.Tick += ProgressTimer_Tick;
            _progressTimer.Interval = TimeSpan.FromSeconds(1);
            _progressTimer.Start();

            Int32.TryParse(this.RecordingAreaRightTextBox.Text, out int right);
            Int32.TryParse(this.RecordingAreaBottomTextBox.Text, out int bottom);
            Int32.TryParse(this.RecordingAreaLeftTextBox.Text, out int left);
            Int32.TryParse(this.RecordingAreaTopTextBox.Text, out int top);

            var selectedDisplay = this.ScreenComboBox.SelectedItem as CheckableRecordableDisplay;

            string audioOutputDevice = AudioOutputsComboBox.SelectedValue as string;
            string audioInputDevice = AudioInputsComboBox.SelectedValue as string;

            var windowsToRecord = this.WindowComboBox.Items.Cast<CheckableRecordableWindow>().Where(x => x.IsSelected).ToList();
            if (windowsToRecord.Count == 0
                && this.WindowComboBox.SelectedItem != null
                && ((CheckableRecordableWindow)this.WindowComboBox.SelectedItem).IsCheckable)
            {
                windowsToRecord.Add((CheckableRecordableWindow)this.WindowComboBox.SelectedItem);
            }

            var displayDevicesToRecord = this.ScreenComboBox.Items.Cast<CheckableRecordableDisplay>().Where(x => x.IsSelected).ToList();
            if (displayDevicesToRecord.Count == 0
                && this.ScreenComboBox.SelectedItem != null
                && ((CheckableRecordableDisplay)this.ScreenComboBox.SelectedItem).IsCheckable)
            {
                displayDevicesToRecord.Add((CheckableRecordableDisplay)this.ScreenComboBox.SelectedItem);
            }

            List<RecordingSource> recordingSources = new List<ScreenRecorderLib.RecordingSource>();
            recordingSources.AddRange(windowsToRecord);
            recordingSources.AddRange(displayDevicesToRecord);

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
                SourceOptions = new SourceOptions
                {
                    RecordingSources = recordingSources,
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
                },
                OverlayOptions = new OverLayOptions
                {
                    Overlays = this.Overlays.Where(x => x.IsEnabled && this.CheckBoxEnableOverlays.IsChecked.GetValueOrDefault(false)).Select(x => x.Overlay).ToList()
                }
            };

            if (_rec == null)
            {
                _rec = Recorder.CreateRecorder(options);
                _rec.OnRecordingComplete += Rec_OnRecordingComplete;
                _rec.OnRecordingFailed += Rec_OnRecordingFailed;
                _rec.OnStatusChanged += Rec_OnStatusChanged;
                _rec.OnSnapshotSaved += Rec_OnSnapshotSaved;
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
        private void Rec_OnSnapshotSaved(object sender, SnapshotSavedEventArgs e)
        {
            //string filepath = e.SnapshotPath;
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

        private void Rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
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

        private void ProgressTimer_Tick(object sender, EventArgs e)
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
            SetScreenComboBoxTitle();
            //if (this.WindowComboBox.SelectedIndex == 0
            //    && this.WindowComboBox.Items.Cast<CheckableRecordableWindow>().Count(x => x.IsSelected) == 0)
            //{
            SetScreenRect();
            //}
        }

        private void SetScreenRect()
        {
            var selectedDisplays = this.ScreenComboBox.Items.Cast<CheckableRecordableDisplay>()
                .Where(x => x.IsSelected)
                .OrderBy(x => x.ScreenCoordinates.Left)
                .ThenBy(x => x.ScreenCoordinates.Top)
                .ToList();

            var selectedWindows = this.WindowComboBox.Items.Cast<CheckableRecordableWindow>()
                .Where(x => x.IsSelected)
                .ToList();

            if (selectedDisplays.Count == 0
                && this.ScreenComboBox.SelectedItem != null
                && ((CheckableRecordableDisplay)this.ScreenComboBox.SelectedItem).IsCheckable)
            {
                selectedDisplays.Add(this.ScreenComboBox.SelectedItem as CheckableRecordableDisplay);
            }
            if (selectedWindows.Count == 0
                && this.WindowComboBox.SelectedItem != null
                && ((CheckableRecordableWindow)this.WindowComboBox.SelectedItem).IsCheckable)
            {
                selectedWindows.Add(this.WindowComboBox.SelectedItem as CheckableRecordableWindow);
            }
            var size = Recorder.GetCombinedOutputSizeForRecordingSources(selectedDisplays.Cast<RecordingSource>().Union(selectedWindows.Cast<RecordingSource>()).ToList());

            this.RecordingAreaRightTextBox.Text = size.Width.ToString();
            this.RecordingAreaBottomTextBox.Text = size.Height.ToString();
            this.RecordingAreaLeftTextBox.Text = 0.ToString();
            this.RecordingAreaTopTextBox.Text = 0.ToString();
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
                SetScreenRect();
            }
            else if (CurrentRecordingApi == RecorderApi.WindowsGraphicsCapture)
            {
                this.WindowComboBox.Visibility = Visibility.Visible;
            }
            if (this.IsLoaded)
            {
                RefreshCaptureTargetItems();
            }
        }

        private void WindowComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (e.AddedItems.Count == 0)
                return;
            SetWindowComboBoxTitle();
            SetScreenRect();
        }
        private void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            RefreshCaptureTargetItems();
        }
        private void RefreshCaptureTargetItems()
        {
            RefreshVideoCaptureItems();
            RefreshScreenComboBox();
            RefreshWindowComboBox();
            RefreshAudioComboBoxes();
        }

        private void RefreshVideoCaptureItems()
        {
            VideoCaptureDevices.Clear();
            VideoCaptureDevices.Add(new MediaDevice { ID = null, Name = "Default capture device" });
            var devices = Recorder.GetSystemVideoCaptureDevices().Select(x => new MediaDevice { ID = x.Key, Name = x.Value });
            foreach (var device in devices)
            {
                VideoCaptureDevices.Add(device);
            }
            (this.Resources["MediaDeviceToDeviceIdConverter"] as MediaDeviceToDeviceIdConverter).MediaDevices = VideoCaptureDevices.ToList();
        }

        private void RefreshScreenComboBox()
        {
            this.ScreenComboBox.Items.Clear();
            this.ScreenComboBox.Items.Add(new CheckableRecordableDisplay()
            {
                DeviceName = null,
                MonitorName = "-- No monitor selected --",
                IsCheckable = false
            });

            var allMonitorsSize = Recorder.GetCombinedOutputSizeForRecordingSources(new List<RecordingSource>() { DisplayRecordingSource.AllMonitors });
            this.ScreenComboBox.Items.Add(new CheckableRecordableDisplay
            {
                DeviceName = DisplayRecordingSource.AllMonitors.DeviceName,
                MonitorName = "All monitors",
                IsCheckable = true,
                ScreenCoordinates = new Rect(0, 0, allMonitorsSize.Width, allMonitorsSize.Height)
            });

            foreach (var target in Recorder.GetDisplays())
            {
                this.ScreenComboBox.Items.Add(new CheckableRecordableDisplay(target));
            }
            if (this.CurrentRecordingApi == RecorderApi.DesktopDuplication)
            {
                ScreenComboBox.SelectedIndex = 1;
            }
            else
            {
                ScreenComboBox.SelectedIndex = 0;
            }
        }
        private void RefreshWindowComboBox()
        {
            this.WindowComboBox.Items.Clear();
            this.WindowComboBox.Items.Add(new CheckableRecordableWindow("-- No window selected --", IntPtr.Zero) { IsCheckable = false });

            foreach (RecordableWindow window in Recorder.GetWindows())
            {
                this.WindowComboBox.Items.Add(new CheckableRecordableWindow(window));
            }
            RefreshWindowSizeAndAvailability();
            WindowComboBox.SelectedIndex = 0;
        }

        private void RefreshWindowSizeAndAvailability()
        {
            foreach (CheckableRecordableWindow window in this.WindowComboBox.Items)
            {
                if (window.Handle == IntPtr.Zero || window.IsMinmimized() || !window.IsValidWindow())
                {
                    window.IsCheckable = false;
                    window.ScreenCoordinates = Rect.Empty;
                }
                else
                {
                    window.UpdateScreenCoordinates();
                    if (!window.ScreenCoordinates.IsEmpty)
                    {
                        window.IsCheckable = true;

                    }
                }
            }
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

        private void ScreenCheckBox_CheckedChanged(object sender, RoutedEventArgs e)
        {
            SetScreenComboBoxTitle();
            SetScreenRect();
        }
        private void SetScreenComboBoxTitle()
        {
            this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
            {
                if (this.ScreenComboBox.Items.Cast<CheckableRecordableDisplay>().Any(x => x.IsSelected))
                {
                    var displays = this.ScreenComboBox.Items.Cast<CheckableRecordableDisplay>().Where(x => x.IsSelected).Select(x => $"{x.MonitorName} ({x.DeviceName})");
                    this.ScreenComboBox.Text = string.Join(", ", displays);
                }
            }));
        }

        private void SetWindowComboBoxTitle()
        {
            this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
            {
                if (this.WindowComboBox.Items.Cast<CheckableRecordableWindow>().Any(x => x.IsSelected))
                {
                    var windows = this.WindowComboBox.Items.Cast<CheckableRecordableWindow>().Where(x => x.IsSelected).Select(x => $"{x.Title}");
                    this.WindowComboBox.Text = string.Join(", ", windows);
                }
            }));
        }

        private void WindowCheckBox_CheckedChanged(object sender, RoutedEventArgs e)
        {
            SetScreenRect();
            SetWindowComboBoxTitle();
        }

        private void WindowComboBox_DropDownOpened(object sender, EventArgs e)
        {
            RefreshWindowSizeAndAvailability();
        }
    }
}
