using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;

namespace TestApp
{
    public interface ICheckableRecordingSource
    {
        bool IsSelected { get; set; }
        bool IsCheckable { get; set; }

        ScreenSize OutputSize { get; set; }
        ScreenPoint Position { get; set; }
        bool IsCustomPositionEnabled { get; set; }
        bool IsCustomOutputSizeEnabled { get; set; }

        void UpdateScreenCoordinates(ScreenPoint position, ScreenSize size);
    }
}
