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
using TestApp.Sources;

namespace TestApp
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window, INotifyPropertyChanged
    {
        private Recorder _rec;
        private DispatcherTimer _progressTimer;
        private DateTimeOffset? _recordingStartTime = null;
        private DateTimeOffset? _recordingPauseTime = null;
        private Stream _outputStream;

        public bool IsRecording { get; set; }
        public RecorderOptions RecorderOptions { get; } = RecorderOptions.Default;
        public ICheckableRecordingSource SelectedRecordingSource { get; set; }
        public ObservableCollection<ICheckableRecordingSource> RecordingSources { get; } = new ObservableCollection<ICheckableRecordingSource>();
        public List<OverlayModel> Overlays { get; } = new List<OverlayModel>();
        public ObservableCollection<AudioDevice> AudioInputsList { get; set; } = new ObservableCollection<AudioDevice>();
        public ObservableCollection<AudioDevice> AudioOutputsList { get; set; } = new ObservableCollection<AudioDevice>();
        public ObservableCollection<RecordableCamera> VideoCaptureDevices { get; set; } = new ObservableCollection<RecordableCamera>();
        public bool IsLogToFileEnabled { get; set; }

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
                        RecorderOptions.OutputOptions.RecorderMode = RecorderMode.Video;
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

        private int _currentFrameNumber;
        public int CurrentFrameNumber
        {
            get { return _currentFrameNumber; }
            set
            {
                if (_currentFrameNumber != value)
                {
                    _currentFrameNumber = value;
                    RaisePropertyChanged(nameof(CurrentFrameNumber));
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
            InitializeDefaultRecorderOptions();
            RecorderOptions.OutputOptions.PropertyChanged += RecorderOptions_PropertyChanged;
            RecorderOptions.SnapshotOptions.PropertyChanged += RecorderOptions_PropertyChanged;
            RecorderOptions.AudioOptions.PropertyChanged += RecorderOptions_PropertyChanged;
            RecorderOptions.MouseOptions.PropertyChanged += RecorderOptions_PropertyChanged;
        }

        private void RecorderOptions_PropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            switch (e.PropertyName)
            {
                case nameof(OutputOptions.RecorderMode):
                case nameof(SnapshotOptions.SnapshotsWithVideo):
                    {
                        UpdateUiState();
                        break;
                    }
                case nameof(AudioOptions.InputVolume):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                .SetDynamicAudioOptions(new DynamicAudioOptions { InputVolume = RecorderOptions.AudioOptions.InputVolume })
                                .Apply();
                        break;
                    }
                case nameof(AudioOptions.OutputVolume):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                .SetDynamicAudioOptions(new DynamicAudioOptions { OutputVolume = RecorderOptions.AudioOptions.OutputVolume })
                                .Apply();
                        break;
                    }
                case nameof(AudioOptions.IsInputDeviceEnabled):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                             .SetDynamicAudioOptions(new DynamicAudioOptions { IsInputDeviceEnabled = RecorderOptions.AudioOptions.IsInputDeviceEnabled })
                             .Apply();
                        break;
                    }
                case nameof(AudioOptions.IsOutputDeviceEnabled):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                             .SetDynamicAudioOptions(new DynamicAudioOptions { IsOutputDeviceEnabled = RecorderOptions.AudioOptions.IsOutputDeviceEnabled })
                             .Apply();
                        break;
                    }
                case nameof(MouseOptions.MouseLeftClickDetectionColor):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                 .SetDynamicMouseOptions(new DynamicMouseOptions { MouseLeftClickDetectionColor = RecorderOptions.MouseOptions.MouseLeftClickDetectionColor })
                                 .Apply();
                        break;
                    }
                case nameof(MouseOptions.MouseRightClickDetectionColor):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                 .SetDynamicMouseOptions(new DynamicMouseOptions { MouseRightClickDetectionColor = RecorderOptions.MouseOptions.MouseRightClickDetectionColor })
                                 .Apply();
                        break;
                    }
                case nameof(MouseOptions.IsMouseClicksDetected):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                 .SetDynamicMouseOptions(new DynamicMouseOptions {  IsMouseClicksDetected = RecorderOptions.MouseOptions.IsMouseClicksDetected })
                                 .Apply();
                        break;
                    }
                case nameof(MouseOptions.IsMousePointerEnabled):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                 .SetDynamicMouseOptions(new DynamicMouseOptions {  IsMousePointerEnabled = RecorderOptions.MouseOptions.IsMousePointerEnabled })
                                 .Apply();
                        break;
                    }
                case nameof(MouseOptions.MouseClickDetectionRadius):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                 .SetDynamicMouseOptions(new DynamicMouseOptions {  MouseClickDetectionRadius = RecorderOptions.MouseOptions.MouseClickDetectionRadius })
                                 .Apply();
                        break;
                    }
                case nameof(MouseOptions.MouseClickDetectionDuration):
                    {
                        _rec?.GetDynamicOptionsBuilder()
                                 .SetDynamicMouseOptions(new DynamicMouseOptions {  MouseClickDetectionDuration = RecorderOptions.MouseOptions.MouseClickDetectionDuration })
                                 .Apply();
                        break;
                    }
                default:
                    break;
            }
        }

        private void InitializeDefaultRecorderOptions()
        {
            RecorderOptions.LogOptions.LogFilePath = "log.txt";
            RecorderOptions.AudioOptions.IsAudioEnabled = true;
            RecorderOptions.VideoEncoderOptions.Framerate = 60;
        }

        private void InitializeDefaultOverlays()
        {
            Overlays.Clear();
            Overlays.Add(new OverlayModel
            {
                Overlay = new VideoCaptureOverlay
                {
                    AnchorPoint = Anchor.TopLeft,
                    Offset = new ScreenSize(100, 100),
                    Size = new ScreenSize(0, 250)

                },
                IsEnabled = true
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new VideoOverlay
                {
                    AnchorPoint = Anchor.TopRight,
                    SourcePath = @"testmedia\cat.mp4",
                    Offset = new ScreenSize(50, 50),
                    Size = new ScreenSize(0, 200)

                },
                IsEnabled = false
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new ImageOverlay
                {
                    AnchorPoint = Anchor.BottomLeft,
                    SourcePath = @"testmedia\alphatest.png",
                    Offset = new ScreenSize(0, 100),
                    Size = new ScreenSize(0, 300)
                },
                IsEnabled = false
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new ImageOverlay()
                {
                    AnchorPoint = Anchor.Center,
                    SourcePath = @"testmedia\giftest.gif",
                    Offset = new ScreenSize(0, 0),
                    Size = new ScreenSize(0, 300)
                },
                IsEnabled = false
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new DisplayOverlay()
                {
                    AnchorPoint = Anchor.TopLeft,
                    Offset = new ScreenSize(400, 100),
                    Size = new ScreenSize(300, 0)
                },
                IsEnabled = false
            });
            Overlays.Add(new OverlayModel
            {
                Overlay = new WindowOverlay()
                {
                    AnchorPoint = Anchor.BottomRight,
                    Offset = new ScreenSize(100, 100),
                    Size = new ScreenSize(300, 0)
                },
                IsEnabled = false
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
                RecordButton.IsEnabled = false;
                return;
            }
            CurrentFrameNumber = 0;
            OutputResultTextBlock.Text = "";
            UpdateProgress();
            string videoPath = "";
            if (RecorderOptions.OutputOptions.RecorderMode == RecorderMode.Video)
            {
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + ".mp4");
            }
            else if (RecorderOptions.OutputOptions.RecorderMode == RecorderMode.Slideshow)
            {
                //For slideshow just give a folder path as input.
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp) + "\\";
            }
            else if (RecorderOptions.OutputOptions.RecorderMode == RecorderMode.Screenshot)
            {
                string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss-ff");
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + GetImageExtension());
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
            RecorderOptions.SourceOptions.RecordingSources = CreateSelectedRecordingSources();
            RecorderOptions.OverlayOptions.Overlays = this.Overlays.Where(x => x.IsEnabled && this.CheckBoxEnableOverlays.IsChecked.GetValueOrDefault(false)).Select(x => x.Overlay).ToList();
            RecorderOptions.VideoEncoderOptions.Encoder = videoEncoder;
            if (!IsLogToFileEnabled)
            {
                RecorderOptions.LogOptions.LogFilePath = "";
            }
            if (_rec == null)
            {
                _rec = Recorder.CreateRecorder(RecorderOptions);
                _rec.OnRecordingComplete += Rec_OnRecordingComplete;
                _rec.OnRecordingFailed += Rec_OnRecordingFailed;
                _rec.OnStatusChanged += Rec_OnStatusChanged;
                _rec.OnSnapshotSaved += Rec_OnSnapshotSaved;
                _rec.OnFrameRecorded += _rec_OnFrameRecorded;
            }
            else
            {
                _rec.SetOptions(RecorderOptions);
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
            IsRecording = true;
        }

        private void _rec_OnFrameRecorded(object sender, FrameRecordedEventArgs e)
        {
            CurrentFrameNumber = e.FrameNumber;
        }

        private List<RecordingSourceBase> CreateSelectedRecordingSources()
        {
            var sourcesToRecord = RecordingSources.Where(x => x.IsSelected).ToList();
            if (sourcesToRecord.Count == 0
                && SelectedRecordingSource != null
                && SelectedRecordingSource.IsCheckable)
            {
                sourcesToRecord.Add(SelectedRecordingSource);
            }
            //We could pass in the sources directly, but since the models have been used for custom dimensions and positions, 
            //we create new ones to pass in, with null values for non-modified values.
            return sourcesToRecord.Select(x =>
            {
                if (x is CheckableRecordableWindow win)
                {
                    return new WindowRecordingSource(win)
                    {
                        OutputSize = win.IsCustomOutputSizeEnabled ? win.OutputSize : null,
                        SourceRect = win.IsCustomOutputSourceRectEnabled ? win.SourceRect : null,
                        Position = win.IsCustomPositionEnabled ? win.Position : null,
                    };
                }
                else if (x is CheckableRecordableDisplay disp)
                {
                    return new DisplayRecordingSource(disp)
                    {
                        OutputSize = disp.IsCustomOutputSizeEnabled ? disp.OutputSize : null,
                        SourceRect = disp.IsCustomOutputSourceRectEnabled ? disp.SourceRect : null,
                        Position = disp.IsCustomPositionEnabled ? disp.Position : null
                    };
                }
                else if (x is CheckableRecordableCamera cam)
                {
                    return new VideoCaptureRecordingSource(cam)
                    {
                        OutputSize = cam.IsCustomOutputSizeEnabled ? cam.OutputSize : null,
                        SourceRect = cam.IsCustomOutputSourceRectEnabled ? cam.SourceRect : null,
                        Position = cam.IsCustomPositionEnabled ? cam.Position : null
                    };
                }
                else if (x is CheckableRecordableImage img)
                {
                    return new ImageRecordingSource(img)
                    {
                        OutputSize = img.IsCustomOutputSizeEnabled ? img.OutputSize : null,
                        SourceRect = img.IsCustomOutputSourceRectEnabled ? img.SourceRect : null,
                        Position = img.IsCustomPositionEnabled ? img.Position : null
                    };
                }
                else if (x is CheckableRecordableVideo vid)
                {
                    return new VideoRecordingSource(vid)
                    {
                        OutputSize = vid.IsCustomOutputSizeEnabled ? vid.OutputSize : null,
                        SourceRect = vid.IsCustomOutputSourceRectEnabled ? vid.SourceRect : null,
                        Position = vid.IsCustomPositionEnabled ? vid.Position : null
                    };
                }
                else
                {
                    return null as RecordingSourceBase;
                }
            }).ToList();
        }

        private string GetImageExtension()
        {
            switch (RecorderOptions.SnapshotOptions.SnapshotFormat)
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
                        this.VideoSettingsPanel.IsEnabled = true;
                        this.LoggingPanel.IsEnabled = true;
                        this.CheckBoxIsAudioEnabled.IsEnabled = true;
                        this.MouseClickModePanel.IsEnabled = true;
                        break;
                    case RecorderStatus.Recording:
                        _recordingStartTime = DateTimeOffset.Now;
                        PauseButton.Visibility = Visibility.Visible;
                        this.FrameNumberTextBlock.Visibility = Visibility.Visible;
                        if (_progressTimer != null)
                            _progressTimer.IsEnabled = true;
                        if (_recordingPauseTime != null)
                        {
                            _recordingStartTime = _recordingStartTime.Value.AddTicks((DateTimeOffset.Now.Subtract(_recordingPauseTime.Value)).Ticks);
                            _recordingPauseTime = null;
                        }
                        RecordButton.Content = "Stop";
                        PauseButton.Content = "Pause";
                        this.StatusTextBlock.Text = "Recording";
                        this.VideoSettingsPanel.IsEnabled = false;
                        this.LoggingPanel.IsEnabled = false;
                        this.MouseClickModePanel.IsEnabled = false;
                        this.CheckBoxIsAudioEnabled.IsEnabled = false;
                        _progressTimer = new DispatcherTimer();
                        _progressTimer.Tick += ProgressTimer_Tick;
                        _progressTimer.Interval = TimeSpan.FromMilliseconds(30);
                        _progressTimer.Start();
                        break;
                    case RecorderStatus.Paused:
                        if (_progressTimer != null)
                            _progressTimer.IsEnabled = false;
                        _recordingPauseTime = DateTimeOffset.Now;
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
            UpdateProgress();
        }
        private void UpdateProgress()
        {
            if (_recordingStartTime != null)
            {
                TimeStampTextBlock.Text = DateTimeOffset.Now.Subtract(_recordingStartTime.Value).ToString("hh\\:mm\\:ss");
            }
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

        private void SourceComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (e.AddedItems.Count == 0)
                return;
            SetSourceComboBoxTitle();
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

        private void RecordingApiComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (e.AddedItems.Count > 0)
            {
                RecorderApi api = (RecorderApi)e.AddedItems[0];
                switch (api)
                {
                    case RecorderApi.DesktopDuplication:
                        this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
                        {
                            ((ComboBox)sender).Text = "DD";
                        }));
                        break;
                    case RecorderApi.WindowsGraphicsCapture:
                        this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
                        {
                            ((ComboBox)sender).Text = "WGC";
                        }));
                        break;
                    default:
                        break;
                }
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
                switch (RecorderOptions.OutputOptions.RecorderMode)
                {
                    case RecorderMode.Video:
                        this.EncoderOptionsPanel.Visibility = Visibility.Visible;
                        this.SnapshotImageFormatPanel.Visibility = RecorderOptions.SnapshotOptions.SnapshotsWithVideo ? Visibility.Visible : Visibility.Collapsed;
                        this.SnapshotsIntervalPanel.Visibility = RecorderOptions.SnapshotOptions.SnapshotsWithVideo ? Visibility.Visible : Visibility.Collapsed;
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
                        this.SnapshotsIntervalPanel.Visibility = RecorderOptions.OutputOptions.RecorderMode == RecorderMode.Slideshow ? Visibility.Visible : Visibility.Collapsed;
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
            RefreshSourceComboBox();
            RefreshAudioComboBoxes();
        }

        private void RefreshVideoCaptureItems()
        {
            VideoCaptureDevices.Clear();
            var devices = Recorder.GetSystemVideoCaptureDevices().ToList();
            foreach (var device in devices)
            {
                VideoCaptureDevices.Add(device);
            }
            (this.Resources["MediaDeviceToDeviceIdConverter"] as MediaDeviceToDeviceIdConverter).MediaDevices = VideoCaptureDevices.ToList();
        }

        private void RefreshSourceComboBox()
        {
            RecordingSources.Clear();
            foreach (var target in Recorder.GetDisplays())
            {
                RecordingSources.Add(new CheckableRecordableDisplay(target));
            }

            foreach (RecordableWindow window in Recorder.GetWindows())
            {
                RecordingSources.Add(new CheckableRecordableWindow(window));
            }

            foreach (RecordableCamera cam in Recorder.GetSystemVideoCaptureDevices())
            {
                RecordingSources.Add(new CheckableRecordableCamera(cam));
            }

            RecordingSources.Add(new CheckableRecordableVideo(@"testmedia\cat.mp4"));
            RecordingSources.Add(new CheckableRecordableImage(@"testmedia\renault.png"));
            RecordingSources.Add(new CheckableRecordableImage(@"testmedia\earth.gif"));

            (this.Resources["RecordableDisplayToDeviceIdConverter"] as RecordableDisplayToDeviceIdConverter).Displays = RecordingSources.Where(x => x is RecordableDisplay).Cast<RecordableDisplay>().ToList();
            (this.Resources["RecordableWindowToHandleConverter"] as RecordableWindowToHandleConverter).Windows = RecordingSources.Where(x => x is RecordableWindow).Cast<RecordableWindow>().ToList();

            var outputDimens = Recorder.GetOutputDimensionsForRecordingSources(RecordingSources.Cast<RecordingSourceBase>());
            foreach (var source in RecordingSources)
            {
                var sourceCoord = outputDimens.OutputCoordinates.FirstOrDefault(x => x.Source == source);
                ScreenPoint position;
                ScreenSize size;
                if (sourceCoord != null)
                {
                    position = source is CheckableRecordableDisplay ? new ScreenPoint(sourceCoord.Coordinates.Left, sourceCoord.Coordinates.Top) : new ScreenPoint(0, 0);
                    size = new ScreenSize(sourceCoord.Coordinates.Width, sourceCoord.Coordinates.Height);
                }
                else
                {
                    position = new ScreenPoint();
                    size = new ScreenSize();
                }
                source.UpdateScreenCoordinates(position, size);
            }
            RefreshWindowSizeAndAvailability();

            SourceComboBox.SelectedIndex = 0;
        }

        private void RefreshWindowSizeAndAvailability()
        {
            var windowSources = RecordingSources.Where(x => x is CheckableRecordableWindow).Cast<CheckableRecordableWindow>();
            var outputDimens = Recorder.GetOutputDimensionsForRecordingSources(windowSources);
            foreach (var source in windowSources)
            {
                var sourceCoord = outputDimens.OutputCoordinates.FirstOrDefault(x => x.Source == source);

                if (sourceCoord != null)
                {
                    var position = new ScreenPoint(0, 0);
                    var size = new ScreenSize(sourceCoord.Coordinates.Width, sourceCoord.Coordinates.Height);
                    source.UpdateScreenCoordinates(position, size);
                }
                else
                {
                    source.UpdateScreenCoordinates(new ScreenPoint(), new ScreenSize());
                }
                source.IsCheckable = true;
            }

        }

        private void RefreshAudioComboBoxes()
        {
            AudioOutputsList.Clear();
            AudioInputsList.Clear();

            AudioOutputsList.Add(new AudioDevice("", "Default playback device"));
            AudioInputsList.Add(new AudioDevice("", "Default recording device"));
            foreach (var outputDevice in Recorder.GetSystemAudioDevices(AudioDeviceSource.OutputDevices))
            {
                AudioOutputsList.Add(outputDevice);
            }
            foreach (var inputDevice in Recorder.GetSystemAudioDevices(AudioDeviceSource.InputDevices))
            {
                AudioInputsList.Add(inputDevice);
            }

            AudioOutputsComboBox.SelectedIndex = 0;
            AudioInputsComboBox.SelectedIndex = 0;
        }

        private void ScreenCheckBox_CheckedChanged(object sender, RoutedEventArgs e)
        {
            SetSourceComboBoxTitle();
            SetOutputDimensions();
            ((CollectionViewSource)Resources["SelectedRecordingSourcesViewSource"]).View.Refresh();
        }
        private void SetSourceComboBoxTitle()
        {
            this.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (Action)(() =>
            {
                if (RecordingSources.Any(x => x.IsSelected))
                {
                    var sources = RecordingSources.Where(x => x.IsSelected).Select(x => x.ToString());
                    this.SourceComboBox.Text = string.Join(", ", sources);
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
                if (!source.IsCustomOutputSourceRectEnabled)
                {
                    source.SourceRect = null;
                }
            }
            var outputDimens = Recorder.GetOutputDimensionsForRecordingSources(selectedSources.Cast<RecordingSourceBase>());
            var allMonitorsSize = outputDimens.CombinedOutputSize;
            if (!IsCustomOutputSourceRectEnabled)
            {
                RecorderOptions.OutputOptions.SourceRect = new ScreenRect(0, 0, allMonitorsSize.Width, allMonitorsSize.Height);
            }
            if (!IsCustomOutputFrameSizeEnabled)
            {
                RecorderOptions.OutputOptions.OutputFrameSize = new ScreenSize(allMonitorsSize.Width, allMonitorsSize.Height);
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
        private void DisplaysViewSource_Filter(object sender, FilterEventArgs e)
        {
            e.Accepted = e.Item is DisplayRecordingSource;
        }
        private void WindowsViewSource_Filter(object sender, FilterEventArgs e)
        {
            e.Accepted = e.Item is RecordableWindow && ((RecordableWindow)e.Item).IsValidWindow() && !((RecordableWindow)e.Item).IsMinmimized();
        }
        private void MainWin_Activated(object sender, EventArgs e)
        {
            SetOutputDimensions();
        }

        private void UserInput_TextInput(object sender, System.Windows.Input.TextCompositionEventArgs e)
        {
            Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Background, (Action)(() =>
            {
                SetOutputDimensions();
            }));
        }

        private void CustomCoordinates_CheckedChanged(object sender, RoutedEventArgs e)
        {
            SetOutputDimensions();
        }

        private void SourceComboBox_DropDownOpened(object sender, EventArgs e)
        {
            RefreshWindowSizeAndAvailability();
        }
    }
}