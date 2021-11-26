using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using ScreenRecorderLib;
using System.IO;


namespace TestAppWinforms
{
    public partial class Form1 : Form
    {
        private Timer _progressTimer;
        private bool _isRecording;
        private int _secondsElapsed;
        Recorder _rec;
        public Form1()
        {
            InitializeComponent();
        }
        private void PauseButton_Click(object sender, EventArgs e)
        {
            if (_rec.Status == RecorderStatus.Paused)
            {
                _rec.Resume();
                return;
            }
            _rec.Pause();
        }
        private void RecordButton_Click(object sender, EventArgs e)
        {
            if (_isRecording)
            {
                _rec.Stop();
                _progressTimer?.Stop();
                _progressTimer = null;
                _secondsElapsed = 0;
                RecordButton.Enabled = false;
                return;
            }
            textBoxResult.Text = "";
            UpdateProgress();


            string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
            string videoPath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + ".mp4");


            _progressTimer = new Timer();
            _progressTimer.Tick += _progressTimer_Tick;
            _progressTimer.Interval = 1000;
            _progressTimer.Start();



            if (_rec == null)
            {
                _rec = Recorder.CreateRecorder();
                _rec.OnRecordingComplete += Rec_OnRecordingComplete;
                _rec.OnRecordingFailed += Rec_OnRecordingFailed;
                _rec.OnStatusChanged += _rec_OnStatusChanged;
                _rec.OnSnapshotSaved += _rec_OnSnapshotSaved;
            }

            _rec.Record(videoPath);
            _secondsElapsed = 0;
            _isRecording = true;
        }

        private void Rec_OnRecordingComplete(object sender, RecordingCompleteEventArgs e)
        {
            BeginInvoke(((Action)(() =>
            {
                string filePath = e.FilePath;

                textBoxResult.Text = filePath;
                PauseButton.Visible = false;
                RecordButton.Text = "Record";
                RecordButton.Enabled = true;
                this.labelStatus.Text = "Completed";
                _isRecording = false;
                CleanupResources();
            })));
        }

        private void Rec_OnRecordingFailed(object sender, RecordingFailedEventArgs e)
        {
            BeginInvoke(((Action)(() =>
            {
                PauseButton.Visible = false;
                RecordButton.Text = "Record";
                RecordButton.Enabled = true;
                labelStatus.Text = "Error:";
                labelError.Visible = true;
                labelError.Text = e.Error;
                _isRecording = false;
                CleanupResources();
            })));
        }

        private void _rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
        {
            BeginInvoke(((Action)(() =>
            {
                labelError.Visible = false;
                switch (e.Status)
                {
                    case RecorderStatus.Idle:
                        this.labelStatus.Text = "Idle";
                        break;
                    case RecorderStatus.Recording:
                        PauseButton.Visible = true;
                        if (_progressTimer != null)
                            _progressTimer.Enabled = true;
                        RecordButton.Text = "Stop";
                        PauseButton.Text = "Pause";
                        this.labelStatus.Text = "Recording";
                        break;
                    case RecorderStatus.Paused:
                        if (_progressTimer != null)
                            _progressTimer.Enabled = false;
                        PauseButton.Text = "Resume";
                        this.labelStatus.Text = "Paused";
                        break;
                    case RecorderStatus.Finishing:
                        PauseButton.Visible = false;
                        this.labelStatus.Text = "Finalizing video";
                        break;
                    default:
                        break;
                }
            })));
        }

        private void _rec_OnSnapshotSaved(object sender, SnapshotSavedEventArgs e)
        {

        }

        private void _progressTimer_Tick(object sender, EventArgs e)
        {
            _secondsElapsed++;
            UpdateProgress();
        }

        private void UpdateProgress()
        {
            labelTimestamp.Text = TimeSpan.FromSeconds(_secondsElapsed).ToString();
        }

        private void CleanupResources()
        {
            _progressTimer?.Stop();
            _progressTimer = null;
            _secondsElapsed = 0;
            _rec?.Dispose();
            _rec = null;
        }

        private void buttonOpenDirectory_Click(object sender, EventArgs e)
        {
            string directory = Path.Combine(Path.GetTempPath(), "ScreenRecorder");
            if (!Directory.Exists(directory))
            {
                Directory.CreateDirectory(directory);
            }
            System.Diagnostics.Process.Start(directory);
        }

        private void buttonDeleteRecordedVideos_Click(object sender, EventArgs e)
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

        private void buttonCopyPath_Click(object sender, EventArgs e)
        {
            if (!string.IsNullOrEmpty(textBoxResult.Text))
            {
                Clipboard.SetText(textBoxResult.Text);
            }
        }
    }
}
