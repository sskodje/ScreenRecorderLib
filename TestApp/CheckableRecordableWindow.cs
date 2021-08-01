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
    public class CheckableRecordableWindow : RecordableWindow, ICheckableRecordingSource
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

        private bool _isCustomPositionEnabled;
        public bool IsCustomPositionEnabled
        {
            get { return _isCustomPositionEnabled; }
            set
            {
                if (_isCustomPositionEnabled != value)
                {
                    _isCustomPositionEnabled = value;
                    OnPropertyChanged(nameof(IsCustomPositionEnabled));
                }
            }
        }

        private bool _isCustomOutputSizeEnabled;
        public bool IsCustomOutputSizeEnabled
        {
            get { return _isCustomOutputSizeEnabled; }
            set
            {
                if (_isCustomOutputSizeEnabled != value)
                {
                    _isCustomOutputSizeEnabled = value;
                    OnPropertyChanged(nameof(IsCustomOutputSizeEnabled));
                }
            }
        }


        public CheckableRecordableWindow(string title, IntPtr handle) : base(title, handle)
        {
            ScreenRect coord = GetScreenCoordinates();
            UpdateScreenCoordinates(new ScreenPoint(0, 0), new ScreenSize(coord.Width, coord.Height));
        }
        public CheckableRecordableWindow(RecordableWindow window) : base(window.Title, window.Handle)
        {
            ScreenRect coord = GetScreenCoordinates();
            UpdateScreenCoordinates(new ScreenPoint(0, 0), new ScreenSize(coord.Width, coord.Height));
        }
        public override string ToString()
        {
            return $"{Title}";
        }

        public void UpdateScreenCoordinates(ScreenPoint position, ScreenSize size)
        {
            if (!IsMinmimized() && IsValidWindow())
            {
                if (!IsCustomOutputSizeEnabled)
                {
                    OutputSize = size;
                }
                if (!IsCustomPositionEnabled)
                {
                    Position = position;
                }
            }
            else
            {
                OutputSize = ScreenSize.Empty;
                Position = ScreenPoint.Empty;

            }
        }
    }
}
