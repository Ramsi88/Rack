#include "ui.hpp"


namespace rack {


void SequentialLayout::onResize() {
	float offset = 0.0;
	for (Widget *child : children) {
		if (!child->visible)
			continue;

		if (orientation == HORIZONTAL_ORIENTATION) {
			child->box.pos.x = padding.x + offset;
			child->box.pos.y = padding.y;
			offset += child->box.size.x + spacing;
		} else {
			child->box.pos.x = padding.x;
			child->box.pos.y = padding.y + offset;
			offset += child->box.size.y + spacing;
		}
	}

	// We're done if left aligned
	if (alignment == LEFT_ALIGNMENT)
		return;

	// Adjust positions based on width of the layout itself
	//TODO: take padding into account
	offset -= spacing;
	if (alignment == RIGHT_ALIGNMENT)
		offset -= (orientation == HORIZONTAL_ORIENTATION ? box.size.x : box.size.y);
	else if (alignment == CENTER_ALIGNMENT)
		offset -= (orientation == HORIZONTAL_ORIENTATION ? box.size.x : box.size.y) / 2.0;
	for (Widget *child : children) {
		if (!child->visible)
			continue;
		(orientation == HORIZONTAL_ORIENTATION ? child->box.pos.x : child->box.pos.y) += offset;
	}

	Widget::onResize();	
}


} // namespace rack
