#include "CaptureBase.h"

SIZE CaptureBase::GetContentOffset(_In_ ContentAnchor anchor, _In_ RECT parentRect, _In_ RECT contentRect)
{
	int leftMargin;
	int topMargin;
	switch (anchor)
	{
		case ContentAnchor::TopLeft:
		default: {
			leftMargin = 0;
			topMargin = 0;
			break;
		}
		case ContentAnchor::TopRight: {
			leftMargin = (int)max(0, round(((double)RectWidth(parentRect) - (double)RectWidth(contentRect))));
			topMargin = 0;
			break;
		}
		case ContentAnchor::Center: {
			leftMargin = (int)max(0, round(((double)RectWidth(parentRect) - (double)RectWidth(contentRect))) / 2);
			topMargin = (int)max(0, round(((double)RectHeight(parentRect) - (double)RectHeight(contentRect))) / 2);
			break;
		}
		case ContentAnchor::BottomLeft: {
			leftMargin = 0;
			topMargin = (int)max(0, round(((double)RectHeight(parentRect) - (double)RectHeight(contentRect))));
			break;
		}
		case ContentAnchor::BottomRight: {
			leftMargin = (int)max(0, round(((double)RectWidth(parentRect) - (double)RectWidth(contentRect))));
			topMargin = (int)max(0, round(((double)RectHeight(parentRect) - (double)RectHeight(contentRect))));
			break;
		}
	}
	return SIZE{ leftMargin,topMargin };
}
