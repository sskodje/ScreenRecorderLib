using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace TestApp
{
    public class DisplayOutput
    {
        public string DisplayName { get; set; }
        public string DeviceName { get; set; }
        public bool IsSelected { get; set; }
        public bool IsCheckable { get; set; } = true;

        public int Width { get; set; }
        public int Height { get; set; }
        public Point Position { get; set; }

        public override string ToString()
        {
            return $"{DisplayName} ({DeviceName})";
        }
    }
}
