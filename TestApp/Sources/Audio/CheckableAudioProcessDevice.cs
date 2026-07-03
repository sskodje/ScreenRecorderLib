using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace TestApp.Sources.Audio
{
    public class CheckableAudioProcessDevice : RecordableAudioProcess, ICheckableAudioRecordingSource
    {

        public CheckableAudioProcessDevice()
        {
        }

        public CheckableAudioProcessDevice(string friendlyName, int processId) : base(friendlyName, processId)
        {
        }
        public CheckableAudioProcessDevice(RecordableAudioProcess device) : base(device.FriendlyName, device.ProcessId)
        {
        }

        private bool _isSelected;
        public bool IsSelected
        {
            get { return _isSelected; }
            set
            {
                if (_isSelected != value)
                {
                    _isSelected = value;
                    OnPropertyChanged(nameof(IsSelected));
                }
            }
        }

        private bool _isCheckable;
        public bool IsCheckable
        {
            get { return _isCheckable; }
            set
            {
                if (_isCheckable != value)
                {
                    _isCheckable = value;
                    OnPropertyChanged(nameof(IsCheckable));
                }
            }
        }

        private double _gain;
        public double Gain
        {
            get { return _gain; }
            set
            {
                if (_gain != value)
                {
                    _gain = value;
                    OnPropertyChanged(nameof(Gain));
                }
            }
        }


        public override string ToString()
        {
            return this.FriendlyName;
        }

    }
}
