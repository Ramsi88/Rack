#include "app.hpp"
#include "plugin.hpp"
#include "window.hpp"
#include <set>
#include <algorithm>


#ifdef TOUCH
static const float itemMargin = 2.0 + 8.0;
#else
static const float itemMargin = 2.0;
#endif


namespace rack {


static std::set<Model*> sFavoriteModels;
static std::string sAuthorFilter;
static ModelTag sTagFilter = NO_TAG;



bool isMatch(std::string s, std::string search) {
	s = stringLowercase(s);
	search = stringLowercase(search);
	return (s.find(search) != std::string::npos);
}

static bool isModelMatch(Model *model, std::string search) {
	if (search.empty())
		return true;
	std::string s;
	s += model->plugin->slug;
	s += " ";
	s += model->author;
	s += " ";
	s += model->name;
	s += " ";
	s += model->slug;
	for (ModelTag tag : model->tags) {
		s += " ";
		s += gTagNames[tag];
	}
	return isMatch(s, search);
}


struct FavoriteRadioButton : RadioButton {
	Model *model = NULL;

	void onAction(EventAction &e) override;
};


struct SeparatorItem : OpaqueWidget {
	SeparatorItem() {
#ifdef TOUCH
		box.size.y = BND_WIDGET_HEIGHT + 2*itemMargin;
#else
		box.size.y = 2*BND_WIDGET_HEIGHT + 2*itemMargin;
#endif
	}

	void setText(std::string text) {
		clearChildren();
#ifdef TOUCH
		Label *label = Widget::create<Label>(Vec(0, itemMargin+2));
#else
		Label *label = Widget::create<Label>(Vec(0, 12 + itemMargin));
#endif
		label->text = text;
		label->fontSize = 20;
		label->color.a *= 0.5;
		addChild(label);
	}
};


struct BrowserListItem : OpaqueWidget {
	bool selected = false;

	BrowserListItem() {
		box.size.y = BND_WIDGET_HEIGHT + 2*itemMargin;
	}

	void draw(NVGcontext *vg) override {
		BNDwidgetState state = selected ? BND_HOVER : BND_DEFAULT;
		bndMenuItem(vg, 0.0, 0.0, box.size.x, box.size.y, state, -1, NULL);
		Widget::draw(vg);
	}

	void onDragStart(EventDragStart &e) override;

	void onDragMove(EventDragMove &e) override;

	void onDragDrop(EventDragDrop &e) override;

	void onDragEnd(EventDragEnd &e) override;

	void doAction() {
		EventAction eAction;
		eAction.consumed = true;
		onAction(eAction);
		if (eAction.consumed) {
			// deletes `this`
			gScene->setOverlay(NULL);
		}
	}
};


struct ModelItem : BrowserListItem {
	Model *model;
	Label *pluginLabel = NULL;

	void setModel(Model *model) {
		clearChildren();
		assert(model);
		this->model = model;

		FavoriteRadioButton *favoriteButton = Widget::create<FavoriteRadioButton>(Vec(8, itemMargin));
		favoriteButton->box.size.x = BND_WIDGET_HEIGHT;
		favoriteButton->label = "★";
		addChild(favoriteButton);

		// Set favorite button initial state
		auto it = sFavoriteModels.find(model);
		if (it != sFavoriteModels.end())
			favoriteButton->setValue(1);
		favoriteButton->model = model;

		Label *nameLabel = Widget::create<Label>(favoriteButton->box.getTopRight());
		nameLabel->text = model->name;
		addChild(nameLabel);

		pluginLabel = Widget::create<Label>(Vec(0, itemMargin));
		pluginLabel->alignment = Label::RIGHT_ALIGNMENT;
		pluginLabel->text = model->author;
		pluginLabel->color.a = 0.5;
		addChild(pluginLabel);
	}

	void onResize() override {
		if (pluginLabel)
			pluginLabel->box.size.x = box.size.x - BND_SCROLLBAR_WIDTH;

		BrowserListItem::onResize();
	}

	void onAction(EventAction &e) override {
		ModuleWidget *moduleWidget = model->createModuleWidget();
		if (!moduleWidget)
			return;
		// Move module nearest to the mouse position
		moduleWidget->box.pos = gRackWidget->lastMousePos.minus(moduleWidget->box.size.div(2));
		gRackWidget->requestModuleBoxNearest(moduleWidget, moduleWidget->box);
		gRackWidget->addModule(moduleWidget);
	}
};


struct AuthorItem : BrowserListItem {
	std::string author;

	void setAuthor(std::string author) {
		clearChildren();
		this->author = author;
		Label *authorLabel = Widget::create<Label>(Vec(0, 0 + itemMargin));
		if (author.empty())
			authorLabel->text = "Show all modules";
		else
			authorLabel->text = author;
		addChild(authorLabel);
	}

	void onAction(EventAction &e) override;
};


struct TagItem : BrowserListItem {
	ModelTag tag;

	void setTag(ModelTag tag) {
		clearChildren();
		this->tag = tag;
		Label *tagLabel = Widget::create<Label>(Vec(0, 0 + itemMargin));
		if (tag == NO_TAG)
			tagLabel->text = "Show all tags";
		else
			tagLabel->text = gTagNames[tag];
		addChild(tagLabel);
	}

	void onAction(EventAction &e) override;
};


struct ClearFilterItem : BrowserListItem {
	ClearFilterItem() {
		Label *label = Widget::create<Label>(Vec(0, 0 + itemMargin));
		label->text = "Back";
		addChild(label);
	}

	void onAction(EventAction &e) override;
};


struct BrowserList : List {
	int selected = 0;

	// Kinetic scrolling
	//TODO: this functionality should me moved to ScrollWidget
	float velocity = 0;
	float amplitude = 0;
	double lastTime = 0;
	double startTime = 0;
	float target = 0;
	bool decelerating = false;
	bool tracking = false;
	float startMousePos = 0;
	float lastOffset = 0;	

	void step() override {
		if (tracking) {
			ScrollWidget *scroll = getAncestorOfType<ScrollWidget>();
			
			double time = glfwGetTime();
			velocity = 0.2*velocity + 0.8 * (scroll->offset.y-lastOffset) / (time-lastTime);

			if (fabsf((gMousePos.y-startMousePos)) > 5)
				selected = -1;

			lastTime = time;
			lastOffset = scroll->offset.y;
		
		} else if (decelerating) {
			double time = glfwGetTime();
			ScrollWidget *scroll = getAncestorOfType<ScrollWidget>();
			float k = 1 - pow((glfwGetTime()-startTime)*1.5, 3);
			if (k >= 0) {
				velocity = amplitude * k;
				float delta = velocity * (glfwGetTime()-lastTime);
				lastTime = time;

				scroll->offset.y += delta;
				scroll->updateForOffsetChange();
			} else
				decelerating = false;
		}

		// Find and select item
		updateSelected();
	}

	void updateSelected() {
		if (1||!tracking || glfwGetTime() - startTime > 0.010) {
			int i = 0;
			for (Widget *child : children) {
				BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
				if (item) {
					item->selected = (i == selected);
					i++;
				}
			}
		}
	}

	void incrementSelection(int delta) {
		selected += delta;
		selected = eucmod(selected, countItems());
  	}

	int countItems() {
		int n = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				n++;
			}
		}
		return n;
	}

	void selectItem(Widget *w) {
		int i = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				if (child == w) {
					selected = i;
					break;
				}
				i++;
			}
		}
	}

	BrowserListItem *getSelectedItem() {
		int i = 0;
		for (Widget *child : children) {
			BrowserListItem *item = dynamic_cast<BrowserListItem*>(child);
			if (item) {
				if (i == selected) {
					return item;
				}
				i++;
			}
		}
		return NULL;
	}

	void scrollSelected() {
		BrowserListItem *item = getSelectedItem();
		if (item) {
			ScrollWidget *parentScroll = dynamic_cast<ScrollWidget*>(parent->parent);
			if (parentScroll)
				parentScroll->scrollTo(item->box);
		}
	}
};


struct ModuleBrowser;

struct SearchModuleField : TextField {
	ModuleBrowser *moduleBrowser;
	void onTextChange() override;
	void onKey(EventKey &e) override;
};


struct ModuleBrowser : OpaqueWidget {
	SearchModuleField *searchField;
	ScrollWidget *moduleScroll;
	BrowserList *moduleList;
	std::set<std::string> availableAuthors;
	std::set<ModelTag> availableTags;

	ModuleBrowser() {
		box.size.x = 450;
		sAuthorFilter = "";
		sTagFilter = NO_TAG;

		// Search
		searchField	= new SearchModuleField();
		searchField->box.size.x = box.size.x;
		searchField->moduleBrowser = this;
#ifndef TOUCH
		addChild(searchField);
#endif

		moduleList = new BrowserList();
		moduleList->box.size = Vec(box.size.x, 0.0);

		// Module Scroll
		moduleScroll = new ScrollWidget();
#ifndef TOUCH
		moduleScroll->box.pos.y = searchField->box.size.y;
#endif
		moduleScroll->box.size.x = box.size.x;
		moduleScroll->container->addChild(moduleList);
		addChild(moduleScroll);

		// Collect authors
		for (Plugin *plugin : gPlugins) {
			for (Model *model : plugin->models) {
				// Insert author
				if (!model->author.empty())
					availableAuthors.insert(model->author);
				// Insert tag
				for (ModelTag tag : model->tags) {
					if (tag != NO_TAG)
						availableTags.insert(tag);
				}
			}
		}
	}

	~ModuleBrowser() {
		gFocusedWidget = NULL;
	}

	void draw(NVGcontext *vg) override {
		bndMenuBackground(vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE);
		Widget::draw(vg);
	}

	void clearSearch() {
		searchField->setText("");
	}

	bool isModelFiltered(Model *model) {
		if (!sAuthorFilter.empty() && model->author != sAuthorFilter)
			return false;
		if (sTagFilter != NO_TAG) {
			auto it = std::find(model->tags.begin(), model->tags.end(), sTagFilter);
			if (it == model->tags.end())
				return false;
		}
		return true;
	}

	void refreshSearch() {
		std::string search = searchField->text;
		moduleList->clearChildren();
#ifdef TOUCH
		moduleList->selected = -1;
#endif
		moduleScroll->offset = Vec(0,0);
		bool filterPage = !(sAuthorFilter.empty() && sTagFilter == NO_TAG);

		if (!filterPage) {
			// Favorites
			if (!sFavoriteModels.empty()) {
				SeparatorItem *item = new SeparatorItem();
				item->setText("Favorites");
				moduleList->addChild(item);
			}
			for (Model *model : sFavoriteModels) {
				if (isModelFiltered(model) && isModelMatch(model, search)) {
					ModelItem *item = new ModelItem();
					item->setModel(model);
					moduleList->addChild(item);
				}
			}
			// Author items
			{
				SeparatorItem *item = new SeparatorItem();
				item->setText("Authors");
				moduleList->addChild(item);
			}
			for (std::string author : availableAuthors) {
				if (isMatch(author, search)) {
					AuthorItem *item = new AuthorItem();
					item->setAuthor(author);
					moduleList->addChild(item);
				}
			}
			// Tag items
			{
				SeparatorItem *item = new SeparatorItem();
				item->setText("Tags");
				moduleList->addChild(item);
			}
			for (ModelTag tag : availableTags) {
				if (isMatch(gTagNames[tag], search)) {
					TagItem *item = new TagItem();
					item->setTag(tag);
					moduleList->addChild(item);
				}
			}
		}
		else {
			// Clear filter
			ClearFilterItem *item = new ClearFilterItem();
			moduleList->addChild(item);
		}

		if (filterPage || !search.empty()) {
			if (!search.empty()) {
				SeparatorItem *item = new SeparatorItem();
				item->setText("Modules");
				moduleList->addChild(item);
			}
			else if (filterPage) {
				SeparatorItem *item = new SeparatorItem();
				if (!sAuthorFilter.empty())
					item->setText(sAuthorFilter);
				else if (sTagFilter != NO_TAG)
					item->setText("Tag: " + gTagNames[sTagFilter]);
				moduleList->addChild(item);
			}
			// Modules
			for (Plugin *plugin : gPlugins) {
				for (Model *model : plugin->models) {
					if (isModelFiltered(model) && isModelMatch(model, search)) {
						ModelItem *item = new ModelItem();
						item->setModel(model);
						moduleList->addChild(item);
					}
				}
			}
		}

		moduleList->onResize();
		repositionSelf();
		moduleScroll->updateForOffsetChange();
	}

	void repositionSelf() {
		box.pos = parent->box.size.minus(box.size).div(2).round();
		box.pos.y = 60;
		box.size.y = parent->box.size.y - 2 * box.pos.y;
		moduleScroll->box.size.y = min(box.size.y - moduleScroll->box.pos.y, moduleList->box.size.y);
		box.size.y = min(box.size.y, moduleScroll->box.getBottomRight().y);
	}

	void step() override {
		gFocusedWidget = searchField;
		Widget::step();
	}
};


// Implementations of inline methods above

void AuthorItem::onAction(EventAction &e) {
	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	sAuthorFilter = author;
	moduleBrowser->clearSearch();
	moduleBrowser->refreshSearch();
	e.consumed = false;
}

void TagItem::onAction(EventAction &e) {
	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	sTagFilter = tag;
	moduleBrowser->clearSearch();
	moduleBrowser->refreshSearch();
	e.consumed = false;
}

void ClearFilterItem::onAction(EventAction &e) {
	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	sAuthorFilter = "";
	sTagFilter = NO_TAG;
	moduleBrowser->refreshSearch();
	e.consumed = false;
}

void FavoriteRadioButton::onAction(EventAction &e) {
	if (!model)
		return;
	if (value) {
		sFavoriteModels.insert(model);
	}
	else {
		auto it = sFavoriteModels.find(model);
		if (it != sFavoriteModels.end())
			sFavoriteModels.erase(it);
	}

	ModuleBrowser *moduleBrowser = getAncestorOfType<ModuleBrowser>();
	if (moduleBrowser)
		moduleBrowser->refreshSearch();
}

void BrowserListItem::onDragStart(EventDragStart &e) {
	BrowserList *list = dynamic_cast<BrowserList*>(parent);
	ScrollWidget *scroll = list->getAncestorOfType<ScrollWidget>();

	list->tracking = true;
	list->velocity = 0;
	list->lastTime = list->startTime = glfwGetTime();
	list->lastOffset = scroll->offset.y;
	list->startMousePos = gMousePos.y;

	if (list->decelerating) {
		list->decelerating = false;
		return;
	}

	list->selectItem(this);
}

void BrowserListItem::onDragMove(EventDragMove &e) {	
	BrowserList *list = dynamic_cast<BrowserList*>(parent);
	ScrollWidget *scroll = list->getAncestorOfType<ScrollWidget>();
	scroll->offset.y -= e.mouseRel.y;
	scroll->updateForOffsetChange();
}

void BrowserListItem::onDragDrop(EventDragDrop &e) {
	if (e.origin != this || !selected)
		return;

	doAction();
}

void BrowserListItem::onDragEnd(EventDragEnd &e) {
	BrowserList *list = dynamic_cast<BrowserList*>(parent);
	list->tracking = false;
	list->updateSelected();

	if (fabsf(list->velocity) > 50) {
		ScrollWidget *scroll = list->getAncestorOfType<ScrollWidget>();

		list->amplitude = list->velocity;
		list->target = scroll->offset.y + list->amplitude;
		list->startTime = glfwGetTime();
		list->decelerating = true;
	}
}

void SearchModuleField::onTextChange() {
	moduleBrowser->refreshSearch();
}

void SearchModuleField::onKey(EventKey &e) {
	moduleBrowser->moduleList->decelerating = false;

	switch (e.key) {
		case GLFW_KEY_ESCAPE: {
			gScene->setOverlay(NULL);
			e.consumed = true;
			return;
		} break;
		case GLFW_KEY_UP: {
			moduleBrowser->moduleList->incrementSelection(-1);
			moduleBrowser->moduleList->scrollSelected();
			e.consumed = true;
		} break;
		case GLFW_KEY_DOWN: {
			moduleBrowser->moduleList->incrementSelection(1);
			moduleBrowser->moduleList->scrollSelected();
			e.consumed = true;
		} break;
		case GLFW_KEY_PAGE_UP: {
			moduleBrowser->moduleList->incrementSelection(-5);
			moduleBrowser->moduleList->scrollSelected();
			e.consumed = true;
		} break;
		case GLFW_KEY_PAGE_DOWN: {
			moduleBrowser->moduleList->incrementSelection(5);
			moduleBrowser->moduleList->scrollSelected();
			e.consumed = true;
		} break;
		case GLFW_KEY_ENTER: {
			BrowserListItem *item = moduleBrowser->moduleList->getSelectedItem();
			if (item) {
				item->doAction();
				e.consumed = true;
				return;
			}
		} break;
	}

	if (!e.consumed) {
		TextField::onKey(e);
	}
}

struct ModuleBrowserOverlay : MenuOverlay {
	ModuleBrowser *moduleBrowser;

	void onResize() override {
		moduleBrowser->repositionSelf();

		MenuOverlay::onResize();
	}
};

// Global functions

void appModuleBrowserCreate() {
	ModuleBrowserOverlay *overlay = new ModuleBrowserOverlay();

	overlay->moduleBrowser = new ModuleBrowser();
	overlay->addChild(overlay->moduleBrowser);

	// Trigger search update
	overlay->moduleBrowser->clearSearch();

	gScene->setOverlay(overlay);
}

json_t *appModuleBrowserToJson() {
	json_t *rootJ = json_object();

	json_t *favoritesJ = json_array();
	for (Model *model : sFavoriteModels) {
		json_t *modelJ = json_object();
		json_object_set_new(modelJ, "plugin", json_string(model->plugin->slug.c_str()));
		json_object_set_new(modelJ, "model", json_string(model->slug.c_str()));
		json_array_append_new(favoritesJ, modelJ);
	}
	json_object_set_new(rootJ, "favorites", favoritesJ);

	return rootJ;
}

void appModuleBrowserFromJson(json_t *rootJ) {
	json_t *favoritesJ = json_object_get(rootJ, "favorites");
	if (favoritesJ) {
		size_t i;
		json_t *favoriteJ;
		json_array_foreach(favoritesJ, i, favoriteJ) {
			json_t *pluginJ = json_object_get(favoriteJ, "plugin");
			json_t *modelJ = json_object_get(favoriteJ, "model");
			if (!pluginJ || !modelJ)
				continue;
			std::string pluginSlug = json_string_value(pluginJ);
			std::string modelSlug = json_string_value(modelJ);
			Model *model = pluginGetModel(pluginSlug, modelSlug);
			if (!model)
				continue;
			sFavoriteModels.insert(model);
		}
	}
}


} // namespace rack
