using ScreenRecorderLib;
using System.ComponentModel;
using System.Windows.Media.Imaging;

namespace TestApp.Sources
{
    public interface ICheckableRecordingSource : INotifyPropertyChanged
    {
        string ID {  get; }
        bool IsSelected { get; set; }
        bool IsCheckable { get; set; }

        ScreenSize OutputSize { get; set; }
        ScreenPoint Position { get; set; }
        ScreenRect SourceRect { get; set; }
        bool IsCustomPositionEnabled { get; set; }
        bool IsCustomOutputSizeEnabled { get; set; }
        bool IsCustomOutputSourceRectEnabled { get; set; }
        bool IsVideoCaptureEnabled { get; set; }
        WriteableBitmap PreviewBitmap { get; set; }
        void UpdateScreenCoordinates(ScreenPoint position, ScreenSize size);
    }
}
