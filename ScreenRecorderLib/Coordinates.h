#pragma once
using namespace System;
using namespace System::ComponentModel;

namespace ScreenRecorderLib {

	/// <summary>
	/// Describes how content is resized to fill its allocated space.
	/// </summary>
	public enum class StretchMode {
		///<summary>The content preserves its original size. </summary>
		None = (int)TextureStretchMode::None,
		///<summary>The content is resized to fill the destination dimensions. The aspect ratio is not preserved. </summary>
		Fill = (int)TextureStretchMode::Fill,
		///<summary>The content is resized to fit in the destination dimensions while it preserves its native aspect ratio.</summary>
		Uniform = (int)TextureStretchMode::Uniform,
		///<summary>
		//     The content is resized to fill the destination dimensions while it preserves
		//     its native aspect ratio. If the aspect ratio of the destination rectangle differs
		//     from the source, the source content is clipped to fit in the destination dimensions.
		///</summary>
		UniformToFill = (int)TextureStretchMode::UniformToFill
	};

	public enum class Anchor {
		TopLeft,
		TopRight,
		Center,
		BottomLeft,
		BottomRight
	};
	public ref class ScreenPoint : public INotifyPropertyChanged {
	internal:
		POINT ToPOINT() {
			POINT pt;
			pt.x = static_cast<long>(round(Left));
			pt.y = static_cast<long>(round(Top));
			return pt;
		}
	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		property double Left;
		property double Top;
		property static ScreenPoint^ Empty {
			ScreenPoint^ get() {
				return gcnew ScreenPoint(double::PositiveInfinity, double::PositiveInfinity);
			}
		}
		ScreenPoint() {}
		ScreenPoint(double left, double top) {
			Left = left;
			Top = top;
		}

		bool ScreenPoint::Equals(Object^ o) override {
			ScreenPoint^ other = static_cast<ScreenPoint^>(o);
			if (other == nullptr) {
				return false;
			}
			return Left == other->Left
				&& Top == other->Top;
		}
	};

	public ref class ScreenSize : public INotifyPropertyChanged {
	internal:
		SIZE ToSIZE() {
			SIZE size;
			size.cx = static_cast<long>(round(Width));
			size.cy = static_cast<long>(round(Height));
			return size;
		}
	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		property double Width;
		property double Height;
		property static ScreenSize^ Empty {
			ScreenSize^ get() {
				return gcnew ScreenSize(double::PositiveInfinity, double::PositiveInfinity);
			}
		}
		ScreenSize() {}
		ScreenSize(double width, double height) {
			Width = width;
			Height = height;
		}

		bool ScreenSize::Equals(Object^ o) override {
			ScreenSize^ other = static_cast<ScreenSize^>(o);
			if (other == nullptr) {
				return false;
			}
			return Width == other->Width
				&& Height == other->Height;
		}
	};

	public ref class ScreenRect : public INotifyPropertyChanged {
	private:
		double _left;
		double _top;
		double _right;
		double _bottom;

	internal:
		RECT ToRECT() {
			RECT rect;
			rect.left = (LONG)round(Left);
			rect.top = (LONG)round(Top);
			rect.right = (LONG)round(Right);
			rect.bottom = (LONG)round(Bottom);
			return rect;
		}

	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		property double Left {
			double get() {
				return _left;
			}
			void set(double value) {
				_left = value;
				OnPropertyChanged("Left");
				OnPropertyChanged("Width");
			}
		}
		property double Top {
			double get() {
				return _top;
			}
			void set(double value) {
				_top = value;
				OnPropertyChanged("Top");
				OnPropertyChanged("Height");
			}
		}
		property double Right {
			double get() {
				return _right;
			}
			void set(double value) {
				_right = value;
				OnPropertyChanged("Right");
				OnPropertyChanged("Width");
			}
		}
		property double Bottom {
			double get() {
				return _bottom;
			}
			void set(double value) {
				_bottom = value;
				OnPropertyChanged("Bottom");
				OnPropertyChanged("Height");
			}
		}
		ScreenRect() {}
		ScreenRect(double left, double top, double width, double height) {
			Left = left;
			Top = top;
			if (double::IsPositiveInfinity(width)) {
				Right = double::NegativeInfinity;
			}
			else {
				Right = left + width;
			}
			if (double::IsPositiveInfinity(height)) {
				Bottom = double::NegativeInfinity;
			}
			else {
				Bottom = top + height;
			}
		}

		property double Width {
			double get() {
				return Right - Left;
			}
		}
		property double Height {
			double get() {
				return Bottom - Top;
			}
		}

		property static ScreenRect^ Empty {
			ScreenRect^ get() {
				return gcnew ScreenRect(double::PositiveInfinity, double::PositiveInfinity, double::NegativeInfinity, double::NegativeInfinity);
			}
		}

		bool ScreenRect::Equals(Object^ o) override {
			ScreenRect^ other = static_cast<ScreenRect^>(o);
			if (other == nullptr) {
				return false;
			}
			return Left == other->Left
				&& Top == other->Top
				&& Width == other->Width
				&& Height == other->Height;
		}
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};
}