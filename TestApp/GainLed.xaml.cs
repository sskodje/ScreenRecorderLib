using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace TestApp
{
    /// <summary>
    /// Interaction logic for GainLed.xaml
    /// </summary>
    public partial class GainLed : UserControl
    {
        public static readonly DependencyProperty GainProperty =
            DependencyProperty.Register(
                nameof(Gain),
                typeof(double),
                typeof(GainLed),
                new PropertyMetadata(0.0, OnGainChanged));

        public double Gain
        {
            get => (double)GetValue(GainProperty);
            set => SetValue(GainProperty, value);
        }

        /// <summary>Gain at which the color starts shifting from green toward yellow.</summary>
        public double YellowThreshold { get; set; } = 0.9;

        /// <summary>Gain at and above which the LED is solid red and starts flashing (clipping).</summary>
        public double ClipThreshold { get; set; } = 1.0;

        /// <summary>How fast the clip LED flashes once clipping.</summary>
        public TimeSpan BlinkInterval { get; set; } = TimeSpan.FromMilliseconds(180);

        private bool _isBlinking;

        private double _lastGain;

        public GainLed()
        {
            InitializeComponent();
            Loaded += (_, __) => UpdateVisual(Gain);
        }

        private static void OnGainChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if (d is GainLed led)
                led.UpdateVisual((double)e.NewValue);
        }

        private void UpdateVisual(double gain)
        {
            if (LitEllipse == null) return; // template not yet loaded

            if (Math.Abs(gain - _lastGain) < 0.01) return;

            _lastGain = gain;
            gain = Math.Max(0.0, gain);
            bool clipping = gain >= ClipThreshold;
            Color color = GetColorForGain(gain);

            LitStopMid.Color = color;
            LitStopOuter.Color = Darken(color, 0.55);
            GlowEffect.Color = color;

            if (clipping)
            {
                StartClipBlink();
            }
            else
            {
                StopClipBlink();
                double intensity = GetIntensityForGain(gain);
                LitEllipse.Opacity = intensity;
                GlowEffect.Opacity = 0.25 + intensity * 0.65;
                GlowEffect.BlurRadius = 8 + intensity * 14;
            }
        }

        private Color GetColorForGain(double gain)
        {
            if (gain < YellowThreshold)
                return Colors.LimeGreen;

            if (gain < ClipThreshold)
            {
                double t = (gain - YellowThreshold) / (ClipThreshold - YellowThreshold);
                return Lerp(Colors.LimeGreen, Colors.Gold, Clamp01(t));
            }

            return Colors.Red;
        }

        /// <summary>
        /// Maps gain to a 0.15-1.0 brightness. Uses a gamma curve (instead of a linear ramp) so the LED
        /// reaches a solidly "lit" look quickly at normal listening levels (~0.3-0.6), since linear opacity
        /// reads as dim to the eye at those levels - then keeps a bit of headroom to brighten further toward clip.
        /// </summary>
        private double GetIntensityForGain(double gain)
        {
            const double gamma = 0.25;
            const double baseline = 0.15;

            double t = Clamp01(gain / ClipThreshold);
            double curved = Math.Pow(t, gamma);
            return Clamp(baseline + (1.0 - baseline) * curved, baseline, 1.0);
        }


        private void StartClipBlink()
        {
            if (_isBlinking) return;
            _isBlinking = true;

            var opacityAnim = new DoubleAnimation
            {
                From = 1.0,
                To = 0.25,
                Duration = BlinkInterval,
                AutoReverse = true,
                RepeatBehavior = RepeatBehavior.Forever
            };

            var glowAnim = new DoubleAnimation
            {
                From = 1.0,
                To = 0.3,
                Duration = BlinkInterval,
                AutoReverse = true,
                RepeatBehavior = RepeatBehavior.Forever
            };

            LitEllipse.BeginAnimation(UIElement.OpacityProperty, opacityAnim);
            GlowEffect.BeginAnimation(System.Windows.Media.Effects.DropShadowEffect.OpacityProperty, glowAnim);
        }

        private void StopClipBlink()
        {
            if (!_isBlinking) return;
            _isBlinking = false;

            LitEllipse.BeginAnimation(UIElement.OpacityProperty, null);
            GlowEffect.BeginAnimation(System.Windows.Media.Effects.DropShadowEffect.OpacityProperty, null);
        }

        private static double Clamp(double value, double min, double max) => Math.Max(min, Math.Min(max, value));
        private static double Clamp01(double value) => Clamp(value, 0.0, 1.0);

        private static Color Lerp(Color a, Color b, double t)
        {
            return Color.FromArgb(
                (byte)(a.A + (b.A - a.A) * t),
                (byte)(a.R + (b.R - a.R) * t),
                (byte)(a.G + (b.G - a.G) * t),
                (byte)(a.B + (b.B - a.B) * t));
        }

        private static Color Darken(Color c, double factor)
        {
            return Color.FromArgb(c.A, (byte)(c.R * factor), (byte)(c.G * factor), (byte)(c.B * factor));
        }

    }
}
