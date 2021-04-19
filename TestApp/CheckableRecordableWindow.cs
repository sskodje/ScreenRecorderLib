using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;

namespace TestApp
{
    public class CheckableRecordableWindow : RecordableWindow, INotifyPropertyChanged
    {
        private bool _isSelected;
        public bool IsSelected
        {
            get { return _isSelected; }
            set
            {
                if (_isSelected != value)
                {
                    _isSelected = value;
                    RaisePropertyChanged("IsSelected");
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
                    RaisePropertyChanged("IsCheckable");
                }
            }
        }


        private Rect _screenCoordinates;
        public Rect ScreenCoordinates
        {
            get { return _screenCoordinates; }
            set
            {
                if (_screenCoordinates != value)
                {
                    _screenCoordinates = value;
                    RaisePropertyChanged("ScreenCoordinates");
                }
            }
        }

        public CheckableRecordableWindow() : base()
        {

        }
        public CheckableRecordableWindow(string title, IntPtr handle) : base(title, handle)
        {
            UpdateScreenCoordinates();
        }
        public CheckableRecordableWindow(RecordableWindow window) : base(window.Title, window.Handle)
        {
            UpdateScreenCoordinates();
        }
        public override string ToString()
        {
            return $"{Title}";
        }

        internal void UpdateScreenCoordinates()
        {
            if (!IsMinmimized())
            {
                var coord = GetScreenCoordinates();
                ScreenCoordinates = new Rect(coord.Left, coord.Top, coord.Width, coord.Height);
            }
            else
            {
                ScreenCoordinates = Rect.Empty;
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
