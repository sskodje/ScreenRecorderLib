using ScreenRecorderLib;
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
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

        public int VideoBitrate { get; set; } = 7000;
        public int VideoFramerate { get; set; } = 60;
        public bool IsAudioEnabled { get; set; } = true;
        public bool IsMousePointerEnabled { get; set; } = true;
        public bool IsFixedFramerate { get; set; } = false;
        public bool IsThrottlingDisabled { get; set; } = false;
        public bool IsHardwareEncodingEnabled { get; set; } = true;
        public bool IsLowLatencyEnabled { get; set; } = false;
        public bool IsMp4FastStartEnabled { get; set; } = false;

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


        //public RecorderMode CurrentRecordingMode { get; set; }
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

        public H264Profile CurrentH264Profile { get; set; } = H264Profile.Main;
        public MainWindow()
        {
            InitializeComponent();
            foreach (var target in WindowsDisplayAPI.DisplayConfig.PathDisplayTarget.GetDisplayTargets())
            {
                this.ScreenComboBox.Items.Add(String.Format("{0} ({1})", target.FriendlyName, target.ConnectorInstance));
            }
            this.ScreenComboBox.SelectedIndex = 0;
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
                IsRecording = false;
                return;
            }
            OutputResultTextBlock.Text = "";

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
                videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + ".png");
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
            RecorderOptions options = new RecorderOptions
            {
                RecorderMode = CurrentRecordingMode,
                IsThrottlingDisabled = this.IsThrottlingDisabled,
                IsHardwareEncodingEnabled = this.IsHardwareEncodingEnabled,
                IsLowLatencyEnabled = this.IsLowLatencyEnabled,
                IsMp4FastStartEnabled = this.IsMp4FastStartEnabled,
                AudioOptions = new AudioOptions
                {
                    Bitrate = AudioBitrate.bitrate_96kbps,
                    Channels = AudioChannels.Stereo,
                    IsAudioEnabled = this.IsAudioEnabled
                },
                VideoOptions = new VideoOptions
                {
                    Bitrate = VideoBitrate * 1000,
                    Framerate = this.VideoFramerate,
                    IsMousePointerEnabled = this.IsMousePointerEnabled,
                    IsFixedFramerate = this.IsFixedFramerate,
                    EncoderProfile = this.CurrentH264Profile
                },
                DisplayOptions = new DisplayOptions(this.ScreenComboBox.SelectedIndex, left, top, right, bottom)
            };
            if (_rec == null)
            {
                _rec = Recorder.CreateRecorder(options);
                _rec.OnRecordingComplete += Rec_OnRecordingComplete;
                _rec.OnRecordingFailed += Rec_OnRecordingFailed;
                _rec.OnStatusChanged += _rec_OnStatusChanged;
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

        private void Rec_OnRecordingFailed(object sender, RecordingFailedEventArgs e)
        {
            Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Normal, (Action)(() =>
            {
                PauseButton.Visibility = Visibility.Hidden;
                RecordButton.Content = "Record";
                StatusTextBlock.Text = "Error:";
                ErrorTextBlock.Visibility = Visibility.Visible;
                ErrorTextBlock.Text = e.Error;
                _outputStream?.Dispose();
                _progressTimer?.Stop();
                _progressTimer = null;
                _secondsElapsed = 0;
                IsRecording = false;
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
                    _outputStream?.Flush();
                    _outputStream?.Dispose();
                }

                OutputResultTextBlock.Text = filePath;
                PauseButton.Visibility = Visibility.Hidden;
                RecordButton.Content = "Record";
                this.StatusTextBlock.Text = "Completed";
                _progressTimer?.Stop();
                _progressTimer = null;
                _secondsElapsed = 0;
                IsRecording = false;
            }));
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
                        break;
                    case RecorderStatus.Recording:
                        PauseButton.Visibility = Visibility.Visible;
                        if (_progressTimer != null)
                            _progressTimer.IsEnabled = true;
                        RecordButton.Content = "Stop";
                        PauseButton.Content = "Pause";
                        this.StatusTextBlock.Text = "Recording";
                        break;
                    case RecorderStatus.Paused:
                        if (_progressTimer != null)
                            _progressTimer.IsEnabled = false;
                        PauseButton.Content = "Resume";
                        this.StatusTextBlock.Text = "Paused";
                        break;
                    case RecorderStatus.Finishing:
                        PauseButton.Visibility = Visibility.Hidden;
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
            var screen = WindowsDisplayAPI.DisplayConfig.PathDisplayTarget.GetDisplayTargets()[this.ScreenComboBox.SelectedIndex];
            this.RecordingAreaRightTextBox.Text = screen.PreferredResolution.Width.ToString();
            this.RecordingAreaBottomTextBox.Text = screen.PreferredResolution.Height.ToString();
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
    }
}
