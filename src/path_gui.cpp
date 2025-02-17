/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file path_gui.cpp %Path building and editing. */

#include "stdafx.h"
#include "map.h"
#include "window.h"
#include "viewport.h"
#include "language.h"
#include "path_build.h"
#include "gui_sprites.h"

/**
 * %Path build GUI.
 * @ingroup gui_group
 */
class PathBuildGui : public GuiWindow {
public:
	PathBuildGui();
	~PathBuildGui();

	void UpdateWidgetSize(WidgetNumber wid_num, BaseWidget *wid) override;
	void DrawWidget(WidgetNumber wid_num, const BaseWidget *wid) const override;

	void OnClick(WidgetNumber wid, const Point16 &pos) override;
	void OnChange(ChangeCode code, uint32 parameter) override;

	void SetButtons();

private:
	Rectangle16 path_type_button_size;             ///< Size of the path type buttons.
	const ImageData *path_type_sprites[PAT_COUNT]; ///< Sprite to use for showing the path type in the Gui.
	bool normal_path_types[PAT_COUNT];             ///< Which path types are normal paths.
	bool queue_path_types[PAT_COUNT];              ///< Which path types are queue paths.
};

/**
 * Widget numbers of the path build GUI.
 * @ingroup gui_group
 */
enum PathBuildWidgets {
	PATH_GUI_SLOPE_DOWN,   ///< Button 'go down'.
	PATH_GUI_SLOPE_FLAT,   ///< Button 'flat'.
	PATH_GUI_SLOPE_UP,     ///< Button 'go up'.
	PATH_GUI_NE_DIRECTION, ///< Build arrow in NE direction.
	PATH_GUI_SE_DIRECTION, ///< Build arrow in SE direction.
	PATH_GUI_SW_DIRECTION, ///< Build arrow in SW direction.
	PATH_GUI_NW_DIRECTION, ///< Build arrow in NW direction.
	PATH_GUI_FORWARD,      ///< Move the arrow a path tile forward.
	PATH_GUI_BACKWARD,     ///< Move the arrow a path tile backward.
	PATH_GUI_LONG,         ///< Build a long path.
	PATH_GUI_BUY,          ///< Buy a path tile.
	PATH_GUI_REMOVE,       ///< Remove a path tile.
	PATH_GUI_NORMAL_PATH0, ///< Button to select #PAT_WOOD type normal paths.
	PATH_GUI_NORMAL_PATH1, ///< Button to select #PAT_TILED type normal paths.
	PATH_GUI_NORMAL_PATH2, ///< Button to select #PAT_ASPHALT type normal paths.
	PATH_GUI_NORMAL_PATH3, ///< Button to select #PAT_CONCRETE type normal paths.
	PATH_GUI_QUEUE_PATH0,  ///< Button to select #PAT_WOOD type queue paths.
	PATH_GUI_QUEUE_PATH1,  ///< Button to select #PAT_TILED type queue paths.
	PATH_GUI_QUEUE_PATH2,  ///< Button to select #PAT_ASPHALT type queue paths.
	PATH_GUI_QUEUE_PATH3,  ///< Button to select #PAT_CONCRETE type queue paths.
	PATH_GUI_SINGLE,       ///< Build a single path.
	PATH_GUI_DIRECTIONAL,  ///< Build a path using the path build interface.
};

static const int SPR_NE_DIRECTION = SPR_GUI_BUILDARROW_START + EDGE_NE; ///< Sprite for building in NE direction.
static const int SPR_SE_DIRECTION = SPR_GUI_BUILDARROW_START + EDGE_SE; ///< Sprite for building in SE direction.
static const int SPR_SW_DIRECTION = SPR_GUI_BUILDARROW_START + EDGE_SW; ///< Sprite for building in SW direction.
static const int SPR_NW_DIRECTION = SPR_GUI_BUILDARROW_START + EDGE_NW; ///< Sprite for building in NW direction.

/**
 * Widget parts of the path build GUI.
 * @ingroup gui_group
 */
static const WidgetPart _path_build_gui_parts[] = {
	Intermediate(0, 1),
		Intermediate(1, 0),
			Widget(WT_TITLEBAR, INVALID_WIDGET_INDEX, COL_RANGE_GREY), SetData(GUI_PATH_GUI_TITLE, GUI_TITLEBAR_TIP),
			Widget(WT_CLOSEBOX, INVALID_WIDGET_INDEX, COL_RANGE_GREY),
		EndContainer(),
		Widget(WT_PANEL, INVALID_WIDGET_INDEX, COL_RANGE_GREY),
			Intermediate(0, 1),
				Intermediate(1, 5), SetPadding(5, 5, 0, 5),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX, COL_RANGE_INVALID), SetFill(1, 0),
					/* Slope down/level/up. */
					Widget(WT_IMAGE_BUTTON, PATH_GUI_SLOPE_DOWN, COL_RANGE_GREY),
							SetData(SPR_GUI_SLOPES_START + TSL_DOWN, GUI_PATH_GUI_SLOPE_DOWN_TIP),
					Widget(WT_IMAGE_BUTTON, PATH_GUI_SLOPE_FLAT, COL_RANGE_GREY), SetPadding(0, 0, 0, 5),
							SetData(SPR_GUI_SLOPES_START + TSL_FLAT, GUI_PATH_GUI_SLOPE_FLAT_TIP),
					Widget(WT_IMAGE_BUTTON, PATH_GUI_SLOPE_UP, COL_RANGE_GREY), SetPadding(0, 0, 0, 5),
							SetData(SPR_GUI_SLOPES_START + TSL_UP, GUI_PATH_GUI_SLOPE_UP_TIP),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX, COL_RANGE_INVALID), SetFill(1, 0),
				Intermediate(1, 3), SetPadding(5, 5, 0, 5),
					/* Four arrows direction. */
					Intermediate(2, 2), SetHorPIP(0, 2, 5), SetVertPIP(0, 2, 0),
						Widget(WT_IMAGE_BUTTON, PATH_GUI_NW_DIRECTION, COL_RANGE_GREY),
								SetData(SPR_NW_DIRECTION, GUI_PATH_GUI_NW_DIRECTION_TIP),
						Widget(WT_IMAGE_BUTTON, PATH_GUI_NE_DIRECTION, COL_RANGE_GREY),
								SetData(SPR_NE_DIRECTION, GUI_PATH_GUI_NE_DIRECTION_TIP),
						Widget(WT_IMAGE_BUTTON, PATH_GUI_SW_DIRECTION, COL_RANGE_GREY),
								SetData(SPR_SW_DIRECTION, GUI_PATH_GUI_SW_DIRECTION_TIP),
						Widget(WT_IMAGE_BUTTON, PATH_GUI_SE_DIRECTION, COL_RANGE_GREY),
								SetData(SPR_SE_DIRECTION, GUI_PATH_GUI_SE_DIRECTION_TIP),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX, COL_RANGE_INVALID), SetFill(1, 0),
					/* Forward/backward. */
					Intermediate(2, 1),
						Widget(WT_TEXT_PUSHBUTTON, PATH_GUI_FORWARD, COL_RANGE_GREY),
								SetData(GUI_PATH_GUI_FORWARD, GUI_PATH_GUI_FORWARD_TIP),
						Widget(WT_TEXT_PUSHBUTTON, PATH_GUI_BACKWARD, COL_RANGE_GREY),
								SetData(GUI_PATH_GUI_BACKWARD, GUI_PATH_GUI_BACKWARD_TIP),
				Intermediate(1, 7), SetPadding(5, 5, 5, 5), SetHorPIP(0, 2, 0),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX,      COL_RANGE_INVALID), SetFill(1, 0),
					Widget(WT_TEXT_BUTTON, PATH_GUI_LONG,       COL_RANGE_GREY),    SetData(GUI_PATH_GUI_LONG, GUI_PATH_GUI_LONG_TIP),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX,      COL_RANGE_INVALID), SetFill(1, 0),
					Widget(WT_TEXT_PUSHBUTTON, PATH_GUI_BUY,    COL_RANGE_GREY),    SetData(GUI_PATH_GUI_BUY, GUI_PATH_GUI_BUY_TIP),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX,      COL_RANGE_INVALID), SetFill(1, 0),
					Widget(WT_TEXT_PUSHBUTTON, PATH_GUI_REMOVE, COL_RANGE_GREY),    SetData(GUI_PATH_GUI_REMOVE, GUI_PATH_GUI_BULLDOZER_TIP),
					Widget(WT_EMPTY, INVALID_WIDGET_INDEX,      COL_RANGE_INVALID), SetFill(1, 0),
				Intermediate(5, 2), SetPadding(5, 2, 2, 2), SetHorPIP(0, 2, 0),
					Widget(WT_CENTERED_TEXT, INVALID_WIDGET_INDEX, COL_RANGE_GREY), SetFill(1, 0), SetData(GUI_PATH_GUI_QUEUE_PATH, STR_NULL),
					Widget(WT_CENTERED_TEXT, INVALID_WIDGET_INDEX, COL_RANGE_GREY), SetFill(1, 0), SetData(GUI_PATH_GUI_NORMAL_PATH, STR_NULL),

					Widget(WT_TEXT_BUTTON, PATH_GUI_QUEUE_PATH0, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),
					Widget(WT_TEXT_BUTTON, PATH_GUI_NORMAL_PATH0, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),

					Widget(WT_TEXT_BUTTON, PATH_GUI_QUEUE_PATH1, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),
					Widget(WT_TEXT_BUTTON, PATH_GUI_NORMAL_PATH1, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),

					Widget(WT_TEXT_BUTTON, PATH_GUI_QUEUE_PATH2, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),
					Widget(WT_TEXT_BUTTON, PATH_GUI_NORMAL_PATH2, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),

					Widget(WT_TEXT_BUTTON, PATH_GUI_QUEUE_PATH3, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),
					Widget(WT_TEXT_BUTTON, PATH_GUI_NORMAL_PATH3, COL_RANGE_GREY), SetData(STR_NULL, STR_NULL),
				Intermediate(1, 2),
					Widget(WT_TEXT_BUTTON, PATH_GUI_SINGLE, COL_RANGE_GREY), SetData(GUI_PATH_GUI_SINGLE, GUI_PATH_GUI_SINGLE_TIP),
					Widget(WT_TEXT_BUTTON, PATH_GUI_DIRECTIONAL, COL_RANGE_GREY), SetData(GUI_PATH_GUI_DIRECTIONAL, GUI_PATH_GUI_DIRECTIONAL_TIP),
			EndContainer(),
	EndContainer(),
};

PathBuildGui::PathBuildGui() : GuiWindow(WC_PATH_BUILDER, ALL_WINDOWS_OF_TYPE), path_type_button_size()
{
	/* Setup path type bits. */
	const SpriteStorage *store = _sprite_manager.GetSprites(64); // GUI size.
	for (int i = 0; i < PAT_COUNT; i++) {
		switch (store->path_sprites[i].status) {
			case PAS_UNUSED:
				this->normal_path_types[i] = false;
				this->queue_path_types[i]  = false;
				this->path_type_sprites[i] = nullptr;
				break;

			case PAS_NORMAL_PATH:
				this->normal_path_types[i] = true;
				this->queue_path_types[i]  = false;
				this->path_type_sprites[i] = store->GetPathSprite(i, PATH_NE_NW_SE_SW, VOR_NORTH);
				this->path_type_button_size.MergeArea(GetSpriteSize(this->path_type_sprites[i]));
				break;

			case PAS_QUEUE_PATH:
				this->normal_path_types[i] = false;
				this->queue_path_types[i]  = true;
				this->path_type_sprites[i] = store->GetPathSprite(i, PATH_NE_SW, VOR_NORTH);
				this->path_type_button_size.MergeArea(GetSpriteSize(this->path_type_sprites[i]));
				break;

			default:
				NOT_REACHED();
		}
	}

	this->SetupWidgetTree(_path_build_gui_parts, lengthof(_path_build_gui_parts));

	/* Select an initial path type if not done already. */
	if (_path_builder.path_type == PAT_INVALID) {
		for (int i = 0; i < PAT_COUNT; i++) {
			if (this->normal_path_types[i]) {
				_path_builder.path_type = (PathType)i;
				break;
			}
			if (_path_builder.path_type == PAT_INVALID && this->queue_path_types[i]) {
				_path_builder.path_type = (PathType)i; // Queue path is better than an invalid one, continue searching.
			}
		}
	}

	_path_builder.SetPathGuiState(true);
}

PathBuildGui::~PathBuildGui()
{
	_path_builder.SetPathGuiState(false);
}

void PathBuildGui::UpdateWidgetSize(WidgetNumber wid_num, BaseWidget *wid)
{
	switch (wid_num) {
		case PATH_GUI_NORMAL_PATH0:
		case PATH_GUI_NORMAL_PATH1:
		case PATH_GUI_NORMAL_PATH2:
		case PATH_GUI_NORMAL_PATH3:
		case PATH_GUI_QUEUE_PATH0:
		case PATH_GUI_QUEUE_PATH1:
		case PATH_GUI_QUEUE_PATH2:
		case PATH_GUI_QUEUE_PATH3:
			wid->min_x = this->path_type_button_size.width + 2 + 2;
			wid->min_y = this->path_type_button_size.height + 2 + 2;
			break;
	}
}

void PathBuildGui::DrawWidget(WidgetNumber wid_num, const BaseWidget *wid) const
{
	static Recolouring recolour; // Never changed.

	switch (wid_num) {
		case PATH_GUI_NORMAL_PATH0:
		case PATH_GUI_NORMAL_PATH1:
		case PATH_GUI_NORMAL_PATH2:
		case PATH_GUI_NORMAL_PATH3:
			if (this->normal_path_types[wid_num - PATH_GUI_NORMAL_PATH0]) {
				const ImageData *img = this->path_type_sprites[wid_num - PATH_GUI_NORMAL_PATH0];
				if (img != nullptr) {
					int dx = (wid->pos.width - path_type_button_size.width) / 2;
					int dy = (wid->pos.height - path_type_button_size.height) / 2;
					Point32 pt(GetWidgetScreenX(wid) + dx - path_type_button_size.base.x, GetWidgetScreenY(wid) + dy - path_type_button_size.base.y);
					_video.BlitImage(pt, img, recolour, GS_NORMAL);
				}
			}
			break;

		case PATH_GUI_QUEUE_PATH0:
		case PATH_GUI_QUEUE_PATH1:
		case PATH_GUI_QUEUE_PATH2:
		case PATH_GUI_QUEUE_PATH3:
			if (this->queue_path_types[wid_num - PATH_GUI_QUEUE_PATH0]) {
				const ImageData *img = this->path_type_sprites[wid_num - PATH_GUI_QUEUE_PATH0];
				if (img != nullptr) {
					int dx = (wid->pos.width - path_type_button_size.width) / 2;
					int dy = (wid->pos.height - path_type_button_size.height) / 2;
					Point32 pt(GetWidgetScreenX(wid) + dx - path_type_button_size.base.x, GetWidgetScreenY(wid) + dy - path_type_button_size.base.y);
					_video.BlitImage(pt, img, recolour, GS_NORMAL);
				}
			}
			break;
	}
}

void PathBuildGui::OnClick(WidgetNumber number, const Point16 &pos)
{
	switch (number) {
		case PATH_GUI_SLOPE_DOWN:
		case PATH_GUI_SLOPE_FLAT:
		case PATH_GUI_SLOPE_UP:
			_path_builder.SelectSlope((TrackSlope)(number - PATH_GUI_SLOPE_DOWN));
			break;

		case PATH_GUI_NE_DIRECTION:
		case PATH_GUI_SE_DIRECTION:
		case PATH_GUI_SW_DIRECTION:
		case PATH_GUI_NW_DIRECTION: {
			Viewport *vp = GetViewport();
			if (vp != nullptr) {
				TileEdge edge = (TileEdge)AddOrientations((ViewOrientation)(number - PATH_GUI_NE_DIRECTION), vp->orientation);
				_path_builder.SelectArrow(edge);
			}
			break;
		}

		case PATH_GUI_FORWARD:
		case PATH_GUI_BACKWARD:
			_path_builder.SelectMovement(number == PATH_GUI_FORWARD);
			break;

		case PATH_GUI_LONG:
			_path_builder.SelectLong();
			break;

		case PATH_GUI_REMOVE:
		case PATH_GUI_BUY:
			_path_builder.SelectBuyRemove(number == PATH_GUI_BUY);
			break;

		case PATH_GUI_NORMAL_PATH0:
		case PATH_GUI_NORMAL_PATH1:
		case PATH_GUI_NORMAL_PATH2:
		case PATH_GUI_NORMAL_PATH3:
			if (this->normal_path_types[number - PATH_GUI_NORMAL_PATH0]) {
				_path_builder.SelectPathType((PathType)(number - PATH_GUI_NORMAL_PATH0));
				this->SetButtons();
			}
			break;

		case PATH_GUI_QUEUE_PATH0:
		case PATH_GUI_QUEUE_PATH1:
		case PATH_GUI_QUEUE_PATH2:
		case PATH_GUI_QUEUE_PATH3:
			if (this->queue_path_types[number - PATH_GUI_QUEUE_PATH0]) {
				_path_builder.SelectPathType((PathType)(number - PATH_GUI_QUEUE_PATH0));
				this->SetButtons();
			}
			break;

		case PATH_GUI_SINGLE:
			_path_builder.SetState(PBS_SINGLE);
			this->SetButtons();
			break;
		case PATH_GUI_DIRECTIONAL:
			_path_builder.SetState(PBS_WAIT_VOXEL);
			this->SetButtons();
			break;

		default:
			break;
	}
}

/** Set the buttons at the path builder GUI. */
void PathBuildGui::SetButtons()
{
	Viewport *vp = GetViewport();
	if (vp == nullptr) return;

	/* Update arrow buttons. */
	uint8 directions = _path_builder.GetAllowedArrows();
	TileEdge sel_dir = _path_builder.GetSelectedArrow();
	for (TileEdge edge = EDGE_BEGIN; edge < EDGE_COUNT; edge++) {
		TileEdge rot_edge = (TileEdge)SubtractOrientations((ViewOrientation)edge, vp->orientation);
		if (((0x11 << edge) & directions) != 0) {
			this->SetWidgetShaded(PATH_GUI_NE_DIRECTION + rot_edge, false);
			this->SetWidgetPressed(PATH_GUI_NE_DIRECTION + rot_edge, edge == sel_dir);
		} else {
			this->SetWidgetShaded(PATH_GUI_NE_DIRECTION + rot_edge, true);
		}
	}
	/* Update the slope buttons. */
	uint8 allowed_slopes = _path_builder.GetAllowedSlopes();
	TrackSlope sel_slope = _path_builder.GetSelectedSlope();
	for (TrackSlope ts = TSL_BEGIN; ts < TSL_COUNT_GENTLE; ts++) {
		bool option_allowed = ((1 << ts) & allowed_slopes) != 0;
		this->SetWidgetShaded(PATH_GUI_SLOPE_DOWN + ts, !option_allowed);
		this->SetWidgetPressed(PATH_GUI_SLOPE_DOWN + ts, ts == sel_slope && option_allowed);
	}
	this->SetWidgetShaded(PATH_GUI_BUY,      !_path_builder.GetBuyIsEnabled());
	this->SetWidgetShaded(PATH_GUI_REMOVE,   !_path_builder.GetRemoveIsEnabled());
	this->SetWidgetShaded(PATH_GUI_FORWARD,  !_path_builder.GetForwardIsEnabled());
	this->SetWidgetShaded(PATH_GUI_BACKWARD, !_path_builder.GetBackwardIsEnabled());
	this->SetWidgetShaded(PATH_GUI_LONG,     !_path_builder.GetLongButtonIsEnabled());
	this->SetWidgetPressed(PATH_GUI_LONG,     _path_builder.GetLongButtonIsPressed());

	for (int i = 0; i < PAT_COUNT; i++) {
		if (this->normal_path_types[i]) {
			this->SetWidgetShaded(PATH_GUI_NORMAL_PATH0 + i, false);
			this->SetWidgetShaded(PATH_GUI_QUEUE_PATH0 + i, true);
			this->SetWidgetPressed(PATH_GUI_NORMAL_PATH0 + i, (i == _path_builder.path_type));
		} else if (this->queue_path_types[i]) {
			this->SetWidgetShaded(PATH_GUI_NORMAL_PATH0 + i, true);
			this->SetWidgetShaded(PATH_GUI_QUEUE_PATH0 + i, false);
			this->SetWidgetPressed(PATH_GUI_QUEUE_PATH0 + i, (i == _path_builder.path_type));
		} else {
			this->SetWidgetShaded(PATH_GUI_NORMAL_PATH0 + i, true);
			this->SetWidgetShaded(PATH_GUI_QUEUE_PATH0 + i, true);
		}
	}

	this->SetWidgetPressed(PATH_GUI_SINGLE,      _path_builder.GetState() == PBS_SINGLE);
	this->SetWidgetPressed(PATH_GUI_DIRECTIONAL, _path_builder.GetState() != PBS_SINGLE);
}

void PathBuildGui::OnChange(ChangeCode code, uint32 parameter)
{
	switch (code) {
		case CHG_UPDATE_BUTTONS:
		case CHG_VIEWPORT_ROTATED:
			this->SetButtons();
			break;

		default:
			break;
	}
}

/**
 * Open the path build GUI.
 * @ingroup gui_group
 */
void ShowPathBuildGui()
{
	if (HighlightWindowByType(WC_PATH_BUILDER, ALL_WINDOWS_OF_TYPE)) return;
	new PathBuildGui;
}
