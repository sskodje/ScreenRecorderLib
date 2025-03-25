using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media.Imaging;

namespace TestApp.Sources
{
    public class RecordableWindowToHandleConverter : DependencyObject, IValueConverter
    {
        public List<RecordableWindow> Windows
        {
            get { return (List<RecordableWindow>)GetValue(WindowsProperty); }
            set { SetValue(WindowsProperty, value); }
        }

        // Using a DependencyProperty as the backing store for Displays.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty WindowsProperty =
            DependencyProperty.Register("Windows", typeof(List<RecordableWindow>), typeof(RecordableWindowToHandleConverter), new PropertyMetadata(null));


        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            RecordableWindow device;
            if (value is null)
            {
                device = Windows.FirstOrDefault();
            }
            else
            {
                device = Windows.FirstOrDefault(x => value.Equals(x.Handle));
            }
            return device;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return (value as RecordableWindow)?.Handle;
        }
    }

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

        private bool _isCustomOutputSourceRectEnabled;
        public bool IsCustomOutputSourceRectEnabled
        {
            get { return _isCustomOutputSourceRectEnabled; }
            set
            {
                if (_isCustomOutputSourceRectEnabled != value)
                {
                    _isCustomOutputSourceRectEnabled = value;
                    OnPropertyChanged(nameof(IsCustomOutputSourceRectEnabled));
                }
            }
        }

        private WriteableBitmap _previewBitmap;
        public WriteableBitmap PreviewBitmap
        {
            get { return _previewBitmap; }
            set
            {
                if (_previewBitmap != value)
                {
                    _previewBitmap = value;
                    OnPropertyChanged(nameof(PreviewBitmap));
                }
            }
        }

        public CheckableRecordableWindow(string title, IntPtr handle) : base(title, handle)
        {

        }
        public CheckableRecordableWindow(RecordableWindow window) : base(window.Title, window.Handle)
        {

        }
        public override string ToString()
        {
            return $"{Title}";
        }

        public void UpdateScreenCoordinates(ScreenPoint position, ScreenSize size)
        {
            if (!IsCustomOutputSourceRectEnabled)
            {
                SourceRect = new ScreenRect(0, 0, size.Width, size.Height);
            }
            if (!IsCustomOutputSizeEnabled)
            {
                OutputSize = size;
            }
            if (!IsCustomPositionEnabled)
            {
                Position = position;
            }
        }
    }
}
