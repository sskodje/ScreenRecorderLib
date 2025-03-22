using ScreenRecorderLib;
using System.ComponentModel;

namespace TestApp
{
    public class OverlayModel : INotifyPropertyChanged
    {
        private bool _isEnabled;
        public bool IsEnabled
        {
            get { return _isEnabled; }
            set
            {
                if (_isEnabled != value)
                {
                    _isEnabled = value;
                    RaisePropertyChanged("IsEnabled");
                }
            }
        }

        private RecordingOverlayBase _overlay;
        public RecordingOverlayBase Overlay
        {
            get { return _overlay; }
            set
            {
                if (_overlay != value)
                {
                    _overlay = value;
                    RaisePropertyChanged("Overlay");
                }
            }
        }

        protected void RaisePropertyChanged(string propertyName)
        {
            PropertyChangedEventHandler handler = PropertyChanged;
            if (handler != null) handler(this, new PropertyChangedEventArgs(propertyName));
        }
        public event PropertyChangedEventHandler PropertyChanged;
    }
}
