using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;

using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;


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
        public ICheckableRecordingSource SelectedRecordingSource { get; set; }
        public ObservableCollection<ICheckableRecordingSource> RecordingSources { get; } = new ObservableCollection<ICheckableRecordingSource>();
        public List<OverlayModel> Overlays { get; } = new List<OverlayModel>();
        public int VideoBitrate { get; set; } = 7000;
        public int VideoFramerate { get; set; } = 60;
        public int VideoQuality { get; set; } = 70;
        public int SnapshotsIntervalMillis { get; set; } = 1000;
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
                    UpdateUiState();
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

        private VideoEncoderFormat _currentVideoEncoderFormat;
        public VideoEncoderFormat CurrentVideoEncoderFormat
        {
            get { return _currentVideoEncoderFormat; }
            set
            {
                if (_currentVideoEncoderFormat != value)
                {
                    _currentVideoEncoderFormat = value;
                    RaisePropertyChanged(nameof(CurrentVideoEncoderFormat));
                }
            }
        }

        private H264BitrateControlMode _currentH264VideoBitrateMode = H264BitrateControlMode.Quality;
        public H264BitrateControlMode CurrentH264VideoBitrateMode
        {
            get { return _currentH264VideoBitrateMode; }
            set
            {
                if (_currentH264VideoBitrateMode != value)
                {
                    _currentH264VideoBitrateMode = value;
                    RaisePropertyChanged(nameof(CurrentH264VideoBitrateMode));
                }
            }
        }

        private H265BitrateControlMode _currentH265VideoBitrateMode = H265BitrateControlMode.Quality;
        public H265BitrateControlMode CurrentH265VideoBitrateMode
        {
            get { return _currentH265VideoBitrateMode; }
            set
            {
                if (_currentH265VideoBitrateMode != value)
                {
                    _currentH265VideoBitrateMode = value;
                    RaisePropertyChanged(nameof(CurrentH265VideoBitrateMode));
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

        private ScreenRect _sourceRect = ScreenRect.Empty;
        public ScreenRect SourceRect
        {
            get { return _sourceRect; }
            set
            {
                if (_sourceRect != value)
                {
                    _sourceRect = value;
                    RaisePropertyChanged(nameof(SourceRect));
                }
            }
        }

        private ScreenSize _outputSize;
        public ScreenSize OutputSize
        {
            get { return _outputSize; }
            set
            {
                if (_outputSize != value)
                {
                    _outputSize = value;
                    RaisePropertyChanged(nameof(OutputSize));
                }
            }
        }
        private bool _isCustomOutputSourceRectEnabled;
        public bool IsCustomOutputSourceRectEnabled
        {
            get { return _isCustomOutputSourceRectEnabled; }
            set
            {
                if (_isCustomOutputSourceRectEnabled != value)
                {
                    _isCustomOutputSourceRectEnabled = value;
                    RaisePropertyChanged(nameof(IsCustomOutputSourceRectEnabled));
                }
            }
        }

        private bool _isCustomOutputFrameSizeEnabled;
        public bool IsCustomOutputFrameSizeEnabled
        {
            get { return _isCustomOutputFrameSizeEnabled; }
            set
            {
                if (_isCustomOutputFrameSizeEnabled != value)
                {
                    _isCustomOutputFrameSizeEnabled = value;
                    RaisePropertyChanged(nameof(IsCustomOutputFrameSizeEnabled));
                }
            }
        }


        public H264Profile CurrentH264Profile { get; set; } = H264Profile.High;
        public H265Profile CurrentH265Profile { get; set; } = H265Profile.Main;

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
            else if (CurrentRecordingMode == RecorderMode.Screenshot)
            {
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss-ff");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + GetImageExtension());
            }
            _progressTimer = new DispatcherTimer();
            _progressTimer.Tick += ProgressTimer_Tick;
            _progressTimer.Interval = TimeSpan.FromSeconds(1);
            _progressTimer.Start();

            var selectedDisplay = this.ScreenComboBox.SelectedItem as CheckableRecordableDisplay;

            string audioOutputDevice = AudioOutputsComboBox.SelectedValue as string;
            string audioInputDevice = AudioInputsComboBox.SelectedValue as string;

            var sourcesToRecord = RecordingSources.Where(x => x.IsSelected).ToList();
            if (sourcesToRecord.Count == 0
                && SelectedRecordingSource != null
                && SelectedRecordingSource.IsCheckable)
            {
                sourcesToRecord.Add(SelectedRecordingSource);
            }

            IVideoEncoder videoEncoder;
            switch (CurrentVideoEncoderFormat)
            {
                default:
                case VideoEncoderFormat.H264:
                    videoEncoder = new H264VideoEncoder { BitrateMode = CurrentH264VideoBitrateMode, EncoderProfile = CurrentH264Profile };
                    break;
                case VideoEncoderFormat.H265:
                    videoEncoder = new H265VideoEncoder { BitrateMode = CurrentH265VideoBitrateMode, EncoderProfile = CurrentH265Profile };
                    break;
            }

            RecorderOptions options = new RecorderOptions
            {
                RecorderMode = CurrentRecordingMode,
                RecorderApi = CurrentRecordingApi,

                LogOptions = new LogOptions
                {
                    IsLogEnabled = this.IsLogEnabled,
                    LogSeverityLevel = this.LogSeverityLevel,
                    LogFilePath = IsLogToFileEnabled ? this.LogFilePath : ""
                },
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
                VideoEncoderOptions = new VideoEncoderOptions
                {
                    Encoder = videoEncoder,
                    Bitrate = VideoBitrate * 1000,
                    Framerate = this.VideoFramerate,
                    Quality = this.VideoQuality,
                    IsFixedFramerate = this.IsFixedFramerate,
                    IsThrottlingDisabled = this.IsThrottlingDisabled,
                    IsHardwareEncodingEnabled = this.IsHardwareEncodingEnabled,
                    IsLowLatencyEnabled = this.IsLowLatencyEnabled,
                    IsMp4FastStartEnabled = this.IsMp4FastStartEnabled,
                    IsFragmentedMp4Enabled = this.IsFragmentedMp4Enabled,
                    FrameSize = IsCustomOutputFrameSizeEnabled ? this.OutputSize : null
                },
                SnapshotOptions = new SnapshotOptions
                {
                    SnapshotFormat = CurrentImageFormat,
                    SnapshotsWithVideo = this.SnapshotsWithVideo,
                    SnapshotsIntervalMillis = this.SnapshotsIntervalMillis
                },
                SourceOptions = new SourceOptions
                {
                    //RecordingSources = sourcesToRecord.Cast<RecordingSourceBase>().ToList(),
                    RecordingSources = sourcesToRecord.Select(x =>
                    {
                        if (x is WindowRecordingSource win)
                        {
                            return new WindowRecordingSource(win.Handle)
                            {
                                IsCursorCaptureEnabled = win.IsCursorCaptureEnabled,
                                OutputSize = x.IsCustomOutputSizeEnabled ? x.OutputSize : null,
                                Position = x.IsCustomPositionEnabled ? x.Position : null
                            };
                        }
                        else if (x is CheckableRecordableDisplay disp)
                        {
                            return new DisplayRecordingSource(disp.DeviceName)
                            {
                                IsCursorCaptureEnabled = disp.IsCursorCaptureEnabled,
                                OutputSize = x.IsCustomOutputSizeEnabled ? x.OutputSize : null,
                                SourceRect = disp.IsCustomOutputSourceRectEnabled ? disp.SourceRect : null,
                                Position = x.IsCustomPositionEnabled ? x.Position : null
                            };
                        }
                        else
                        {
                            return null as RecordingSourceBase;
                        }
                    }).ToList(),
                    SourceRect = IsCustomOutputSourceRectEnabled ? this.SourceRect : null
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
            SetOutputDimensions();
            ((CollectionViewSource)Resources["SelectedRecordingSourcesViewSource"]).View.Refresh();
        }

        private void Hyperlink_Click(object sender, RoutedEventArgs e)
        {
            Process.Start(this.OutputResultTextBlock.Text);
        }

        private void TextBox_GotFocus(object sender, RoutedEventArgs e)
        {
            Dispatcher.BeginInvoke(DispatcherPriority.Background, (Action)(() =>
             {
                 ((TextBox)sender).SelectAll();
             }));
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

        private void VideoEncoderComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            UpdateUiState();
        }
        private void H264VideoBitrateModeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            UpdateUiState();
        }
        private void H265VideoBitrateModeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            UpdateUiState();
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
            UpdateUiState();
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
            UpdateUiState();
            if (this.IsLoaded)
            {
                RefreshCaptureTargetItems();
            }
        }

        private void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            RefreshCaptureTargetItems();
        }

        private void UpdateUiState()
        {
            if (this.IsLoaded)
            {
                switch (CurrentRecordingMode)
                {
                    case RecorderMode.Video:
                        this.EncoderOptionsPanel.Visibility = Visibility.Visible;
                        this.SnapshotImageFormatPanel.Visibility = SnapshotsWithVideo ? Visibility.Visible : Visibility.Collapsed;
                        this.SnapshotsIntervalPanel.Visibility = SnapshotsWithVideo ? Visibility.Visible : Visibility.Collapsed;
                        this.EncoderOptionsPanel.Visibility = Visibility.Visible;
                        this.CheckBoxSnapshotsWithVideo.Visibility = Visibility.Visible;
                        this.VideoEncoderOptionsPanel.Visibility = Visibility.Visible;
                        this.VideoBitratePanel.Visibility = Visibility.Visible;
                        this.VideoQualityPanel.Visibility = Visibility.Visible;
                        this.AudioPanel.Visibility = Visibility.Visible;

                        switch (CurrentVideoEncoderFormat)
                        {
                            case VideoEncoderFormat.H264:
                                H264OptionsPanel.Visibility = Visibility.Visible;
                                H265OptionsPanel.Visibility = Visibility.Collapsed;
                                VideoQualityPanel.Visibility = CurrentH264VideoBitrateMode == H264BitrateControlMode.Quality ? Visibility.Visible : Visibility.Collapsed;
                                VideoQualityPanel.Visibility = CurrentH264VideoBitrateMode == H264BitrateControlMode.Quality ? Visibility.Visible : Visibility.Collapsed;
                                break;
                            case VideoEncoderFormat.H265:
                                H264OptionsPanel.Visibility = Visibility.Collapsed;
                                H265OptionsPanel.Visibility = Visibility.Visible;
                                VideoQualityPanel.Visibility = CurrentH265VideoBitrateMode == H265BitrateControlMode.Quality ? Visibility.Visible : Visibility.Collapsed;
                                VideoBitratePanel.Visibility = CurrentH265VideoBitrateMode == H265BitrateControlMode.Quality ? Visibility.Collapsed : Visibility.Visible;
                                break;
                            default:
                                break;
                        }
                        break;
                    case RecorderMode.Slideshow:
                    case RecorderMode.Screenshot:
                        this.EncoderOptionsPanel.Visibility = Visibility.Collapsed;
                        this.SnapshotImageFormatPanel.Visibility = Visibility.Visible;
                        this.SnapshotsIntervalPanel.Visibility = CurrentRecordingMode == RecorderMode.Slideshow ? Visibility.Visible : Visibility.Collapsed;
                        this.EncoderOptionsPanel.Visibility = Visibility.Collapsed;
                        this.CheckBoxSnapshotsWithVideo.Visibility = Visibility.Collapsed;
                        this.VideoEncoderOptionsPanel.Visibility = Visibility.Collapsed;
                        this.VideoBitratePanel.Visibility = Visibility.Collapsed;
                        this.VideoQualityPanel.Visibility = Visibility.Collapsed;
                        this.AudioPanel.Visibility = Visibility.Collapsed;
                        break;
                    default:
                        break;
                }
            }
        }

        private void RefreshCaptureTargetItems()
        {
            RefreshVideoCaptureItems();
            RefreshScreenComboBox();
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
            foreach (CheckableRecordableDisplay display in RecordingSources.ToList().Where(x => x is CheckableRecordableDisplay))
            {
                RecordingSources.Remove(display);
            }

            foreach (var target in Recorder.GetDisplays())
            {
                RecordingSources.Add(new CheckableRecordableDisplay(target));
            }

            foreach (CheckableRecordableWindow window in RecordingSources.ToList().Where(x => x is CheckableRecordableWindow))
            {
                RecordingSources.Remove(window);
            }
            foreach (RecordableWindow window in Recorder.GetWindows())
            {
                RecordingSources.Add(new CheckableRecordableWindow(window));
            }
            RefreshWindowSizeAndAvailability();

            ScreenComboBox.SelectedIndex = 0;
        }

        private void RefreshWindowSizeAndAvailability()
        {
            //var outputDimens = Recorder.GetOutputDimensionsForRecordingSources(RecordingSources.Cast<RecordingSourceBase>());
            foreach (CheckableRecordableWindow window in RecordingSources.Where(x => x is CheckableRecordableWindow))
            {
                //var dimens = outputDimens.OutputCoordinates.FirstOrDefault(x => x.Source == window);
                //if (dimens != null)
                //{
                if (!window.IsCustomPositionEnabled && !window.IsCustomOutputSizeEnabled)
                {
                    var dimens = window.GetScreenCoordinates();
                    window.UpdateScreenCoordinates(new ScreenPoint(0, 0), new ScreenSize(dimens.Width, dimens.Height));
                }
                window.IsCheckable = window.OutputSize != ScreenSize.Empty;
                //}
            }
            //var outputDimens = Recorder.GetOutputDimensionsForRecordingSources(RecordingSources.Where(x => !x.IsSelected).Cast<RecordingSourceBase>());
            //foreach (CheckableRecordableWindow window in RecordingSources.Where(x => x is CheckableRecordableWindow && !x.IsSelected))
            //{
            //    var dimens = outputDimens.OutputCoordinates.FirstOrDefault(x => x.Source == window);
            //    if (dimens != null)
            //    {
            //        window.UpdateScreenCoordinates(new ScreenRect(0, 0, dimens.Coordinates.Width, dimens.Coordinates.Height));
            //    }
            //    else
            //    {
            //        window.UpdateScreenCoordinates(ScreenRect.Empty);
            //    }
            //    window.IsCheckable = window.OutputSize != ScreenSize.Empty;
            //}
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
            SetOutputDimensions();
            ((CollectionViewSource)Resources["SelectedRecordingSourcesViewSource"]).View.Refresh();
        }
        private void SetScreenComboBoxTitle()
        {
            this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
            {
                if (RecordingSources.Any(x => x.IsSelected))
                {
                    var sources = RecordingSources.Where(x => x.IsSelected).Select(x => x.ToString());
                    this.ScreenComboBox.Text = string.Join(", ", sources);
                }
            }));
        }
        private void SetOutputDimensions()
        {
            List<ICheckableRecordingSource> selectedSources = RecordingSources.Where(x => x.IsSelected && x.IsCheckable).ToList();
            if (selectedSources.Count == 0)
            {
                selectedSources.Add(SelectedRecordingSource);
            }

            foreach (var source in selectedSources)
            {
                if (!source.IsCustomPositionEnabled)
                {
                    source.Position = null;
                }
                if (!source.IsCustomOutputSizeEnabled)
                {
                    source.OutputSize = null;
                }
                if (source is CheckableRecordableDisplay disp && !disp.IsCustomOutputSourceRectEnabled)
                {
                    disp.SourceRect = null;
                }
            }
            var outputDimens = Recorder.GetOutputDimensionsForRecordingSources(selectedSources.Cast<RecordingSourceBase>());
            var allMonitorsSize = outputDimens.CombinedOutputSize;
            if (!IsCustomOutputSourceRectEnabled)
            {
                SourceRect = new ScreenRect(0, 0, allMonitorsSize.Width, allMonitorsSize.Height);
            }
            if (!IsCustomOutputFrameSizeEnabled)
            {
                OutputSize = new ScreenSize(allMonitorsSize.Width, allMonitorsSize.Height);
            }
            foreach (SourceCoordinates sourceCoord in outputDimens.OutputCoordinates)
            {
                var source = selectedSources.FirstOrDefault(x => x == sourceCoord.Source);
                if (source != null)
                {
                    source.UpdateScreenCoordinates(new ScreenPoint(sourceCoord.Coordinates.Left, sourceCoord.Coordinates.Top), new ScreenSize(sourceCoord.Coordinates.Width, sourceCoord.Coordinates.Height));
                }
            }
        }
        private void RecordingSourcesViewSource_Filter(object sender, System.Windows.Data.FilterEventArgs e)
        {
            if (CurrentRecordingApi == RecorderApi.DesktopDuplication)
            {
                e.Accepted = e.Item is DisplayRecordingSource;
            }
            else if (CurrentRecordingApi == RecorderApi.WindowsGraphicsCapture)
            {
                e.Accepted = true;
            }
            else
            {
                e.Accepted = false;
            }
        }

        private void WindowRecordingSourcesViewSource_Filter(object sender, System.Windows.Data.FilterEventArgs e)
        {
            e.Accepted = e.Item is WindowRecordingSource;
        }

        private void SelectedRecordingSourcesViewSource_Filter(object sender, System.Windows.Data.FilterEventArgs e)
        {
            if (!RecordingSources.Any(x => x.IsSelected))
            {
                e.Accepted = e.Item == SelectedRecordingSource
                    && SelectedRecordingSource.IsCheckable;
            }
            else
            {
                e.Accepted = ((ICheckableRecordingSource)e.Item).IsSelected
                    && ((ICheckableRecordingSource)e.Item).IsCheckable;
            }
        }

        private void MainWin_Activated(object sender, EventArgs e)
        {
            RefreshWindowSizeAndAvailability();
            SetOutputDimensions();
            ((CollectionViewSource)Resources["SelectedRecordingSourcesViewSource"]).View.Refresh();
        }

        private void UserInput_TextInput(object sender, System.Windows.Input.TextCompositionEventArgs e)
        {
             SetOutputDimensions();
        }

        private void CustomCoordinates_CheckedChanged(object sender, RoutedEventArgs e)
        {
            SetOutputDimensions();
        }
    }
}