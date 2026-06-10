using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace TestApp.Sources.Audio
{
    public class CheckableAudioLoopbackDevice : RecordableAudioLoopbackDevice, ICheckableAudioRecordingSource
    {

        public CheckableAudioLoopbackDevice()
        {
        }

        public CheckableAudioLoopbackDevice(string friendlyName, string deviceName) : base(friendlyName, deviceName)
        {
        }
        public CheckableAudioLoopbackDevice(RecordableAudioLoopbackDevice device) : base(device.FriendlyName, device.DeviceName, device.IsDefaultDevice)
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
        public override string ToString()
        {
            return this.FriendlyName;
        }
    }
}
