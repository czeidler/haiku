/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef LAYER_VIEW_DEFINITIONS_H
#define LAYER_VIEW_DEFINITIONS_H


const rgb_color kBoxColor = ui_color(B_PANEL_BACKGROUND_COLOR);
const rgb_color kBorderLightColor= tint_color(kBoxColor, B_LIGHTEN_2_TINT);
const rgb_color kBorderMediumColor= tint_color(kBoxColor, B_DARKEN_2_TINT);
const rgb_color kBorderDarkColor = tint_color(kBorderMediumColor,
	B_DARKEN_2_TINT); 

const rgb_color kBackGroundColor = kBorderLightColor;

const rgb_color kLightBlueColor = {165, 182, 198, 0};
const rgb_color kBlueColor = {0, 0, 255, 255};
const rgb_color kMediumBlueColor = tint_color(kLightBlueColor, B_DARKEN_2_TINT); 

const rgb_color kTitleColor = {0, 0, 0};
const rgb_color kFocusTitleColor = {255, 255, 255};
const rgb_color kFocusBoxColor = kMediumBlueColor;

const rgb_color kShadowColor = {0, 0, 0, 50};

const rgb_color kCompatibleSlotColor = {255, 255, 144};
const rgb_color kDockNowColor = {190, 255, 110};
const rgb_color kSelectedColor = {190, 0, 0};

const int kRasterSize = 5;
const float kBoxBorderSpace = 3;

const float kSocketSize = 6;
const float kWireOffset = 5;


#endif // LAYER_VIEW_DEFINITIONS_H
