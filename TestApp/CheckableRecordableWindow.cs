using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace TestApp
{
    public class CheckableRecordableWindow : RecordableWindow
    {
        public bool IsSelected { get; set; }
        public bool IsCheckable { get; set; } = true;
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
    }
}
