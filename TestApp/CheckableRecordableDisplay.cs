using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using ScreenRecorderLib;
namespace TestApp
{
    public class CheckableRecordableDisplay : RecordableDisplay
    {
        public bool IsSelected { get; set; }
        public bool IsCheckable { get; set; } = true;

        public Rect ScreenCoordinates { get; set; }
        public CheckableRecordableDisplay() : base()
        {

        }
        public CheckableRecordableDisplay(string monitorName, string deviceName) : base(monitorName, deviceName)
        {
            UpdateScreenCoordinates();
        }
        public CheckableRecordableDisplay(RecordableDisplay disp) : base(disp.MonitorName, disp.DeviceName)
        {
            UpdateScreenCoordinates();
        }

        public override string ToString()
        {
            return $"{MonitorName} ({DeviceName})";
        }

        internal void UpdateScreenCoordinates()
        {
            var coord = GetScreenCoordinates();
            ScreenCoordinates = new Rect(coord.Left, coord.Top, coord.Width, coord.Height);
        }
    }
}
