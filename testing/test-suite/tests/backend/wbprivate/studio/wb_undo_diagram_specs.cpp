/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2026 dev4fun. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA 
 */

#include "grtdb/db_object_helpers.h"
#include "model/wb_history_tree.h"
#include "wb_overview.h"
#include "model/wb_model_diagram_form.h"
#include "model/wb_component_basic.h"

#include "stub/stub_utilities.h"

#include "gtest/gtest.h"
#include "grt_test_helpers.h"
#include "wb_test_helpers.h"
#include <filesystem>

using namespace bec;
using namespace wb;
using namespace grt;


namespace {

//----------------------------------------------------------------------------------------------------------------------

static mforms::DialogResult message_ok_callback() {
  return mforms::ResultOk;
}

//----------------------------------------------------------------------------------------------------------------------

static mforms::DialogResult message_cancel_callback() {
  return mforms::ResultCancel;
}

//----------------------------------------------------------------------------------------------------------------------


struct TestData {
  std::unique_ptr<SqlStudioTester> tester;
  UndoManager *um = nullptr;
  OverviewBE *overview = nullptr;
  ModelDiagramForm *diagramForm = nullptr;
  model_DiagramRef diagram;
  size_t lastUndoStackSize;
  size_t lastRedoStackSize;

  std::string dataDir;

  #include "wb_undo_helpers.h"

  void placeFigureWithTool(const std::string &tool, double x = 10, double y = 10) {
    diagramForm->set_tool(tool);
    diagramForm->handle_mouse_button(mdc::ButtonLeft, true, static_cast<int>(x), static_cast<int>(y),
                                    (mdc::EventState)0);
  }

  //--------------------------------------------------------------------------------------------------------------------

  template <class C>
  void resizeObject(ModelDiagramForm *diagramForm, C obj, double w, double h) {
    double px, py;

    px = obj->left() + obj->width() + 1;
    py = obj->top() + obj->height() + 1;

    // Click once to select the layer.
    diagramForm->handle_mouse_button(mdc::ButtonLeft, true, (int)px - 20, (int)py - 20, mdc::SNone);
    diagramForm->handle_mouse_button(mdc::ButtonLeft, false, (int)px - 20, (int)py - 20, mdc::SNone);

    // Resize it by dragging the lower right resize handle.
    diagramForm->handle_mouse_button(mdc::ButtonLeft, true, (int)px, (int)py, mdc::SNone);
    diagramForm->handle_mouse_move((int)(obj->left() + w), (int)(obj->top() + h), mdc::SLeftButtonMask);

    // We cannot switch auto scrolling off in the diagram so we revert its effect when we performed mouse dragging.
    diagramForm->get_view()->set_offset(base::Point(0, 0));
    diagramForm->handle_mouse_button(mdc::ButtonLeft, false, (int)(obj->left() + w), (int)(obj->top() + h),
                                     mdc::SLeftButtonMask);
  }

  //--------------------------------------------------------------------------------------------------------------------

  template <class C>
  void dragObject(ModelDiagramForm *diagramForm, C object, double deltaX, double deltaY) {
    double x = object->left();
    double y = object->top();

    diagramForm->handle_mouse_button(mdc::ButtonLeft, true, (int)(x + 20), (int)(y + 13), mdc::SNone);
    diagramForm->handle_mouse_move((int)(x + deltaX + 20), (int)(y + deltaY + 13), mdc::SLeftButtonMask);
    diagramForm->handle_mouse_button(mdc::ButtonLeft, false, (int)(x + deltaX + 20), (int)(y + deltaY + 13),
                                     mdc::SLeftButtonMask);
  }

  //--------------------------------------------------------------------------------------------------------------------

};

class UndoRedoForDiagramActionsInSqlStudioTest : public ::testing::Test {
protected:
  std::unique_ptr<TestData> data = std::make_unique<TestData>();

  void SetUp() override {

    data->tester.reset(new SqlStudioTester());
    data->tester->initializeRuntime();
    data->dataDir = std::filesystem::absolute("data").lexically_normal().u8string();

    data->um = grt::GRT::get()->get_undo_manager();
    data->overview = wb::WBContextUI::get()->get_physical_overview();

    data->lastUndoStackSize = 0;
    data->lastRedoStackSize = 0;
    ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/undo_test_model1.mwb"))
      << "open_document";
    ASSERT_EQ(data->tester->getCatalog()->schemata().count(), 1U) << "schemas";

    db_SchemaRef schema(data->tester->getCatalog()->schemata()[0]);

    // Make sure the loaded model contains expected number of things.
    EXPECT_EQ(schema->tables().count(), 4U) << "tables";
    EXPECT_EQ(schema->views().count(), 1U) << "views";
    EXPECT_EQ(schema->routineGroups().count(), 1U) << "groups";

    ASSERT_EQ(data->tester->getPmodel()->diagrams().count(), 1U) << "diagrams";
    data->diagram = data->tester->getPmodel()->diagrams()[0];

    EXPECT_EQ(data->diagram->figures().count(), 5U) << "figures";
    EXPECT_EQ(data->diagram->layers().count(), 1U) << "layers";

    data->tester->openAllDiagrams();
    data->tester->syncView();

    data->diagramForm = data->tester->wb->get_model_context()->get_diagram_form_for_diagram_id(data->tester->getPview().id());
    ASSERT_NE(data->diagramForm, nullptr) << "Diagram form is invalid";

    mforms::ToolBar *toolbar = data->diagramForm->get_tools_toolbar();
    EXPECT_NE(toolbar, nullptr) << "Toolbar creation failed";

    wb::WBContextUI::get()->set_active_form(data->diagramForm);
    EXPECT_EQ(data->um->get_undo_stack().size(), 0U) << "undo stack is empty";

    // Keep the model open for this test's diagram actions.
  }

  void TearDown() override {
    EXPECT_TRUE(data->tester->closeDocument()) << "Could not close document";
    data->tester->wb->close_document_finish();
  }

  //--------------------------------------------------------------------------------------------------------------------

  };

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceTable) {
    db_SchemaRef schema = data->tester->getCatalog()->schemata()[0];
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_root_figure_count = data->diagram->rootLayer()->figures().count();
    size_t old_object_count = schema->tables().count();

    WBComponentPhysical *compo = wb::WBContextUI::get()->get_wb()->get_component<WBComponentPhysical>();

    compo->place_new_db_object(data->diagramForm, base::Point(10, 10), wb::ObjectTable);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add";
    EXPECT_EQ(schema->tables().count(), old_object_count + 1) << "table add";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count) << "figure add undo";
    EXPECT_EQ(schema->tables().count(), old_object_count) << "table add undo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count) << "figure root add undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add redo";
    EXPECT_EQ(schema->tables().count(), old_object_count + 1) << "table add redo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceView) {
    db_SchemaRef schema = data->tester->getCatalog()->schemata()[0];
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_root_figure_count = data->diagram->rootLayer()->figures().count();
    size_t old_object_count = schema->views().count();

    WBComponentPhysical *compo = wb::WBContextUI::get()->get_wb()->get_component<WBComponentPhysical>();

    compo->place_new_db_object(data->diagramForm, base::Point(10, 10), wb::ObjectView);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add";
    EXPECT_EQ(schema->views().count(), old_object_count + 1) << "data->diagramForm add";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count) << "figure add undo";
    EXPECT_EQ(schema->views().count(), old_object_count) << "data->diagramForm add undo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count) << "figure root add undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add redo";
    EXPECT_EQ(schema->views().count(), old_object_count + 1) << "data->diagramForm add redo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceRoutineGroup) {
    db_SchemaRef schema = data->tester->getCatalog()->schemata()[0];
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_root_figure_count = data->diagram->rootLayer()->figures().count();
    size_t old_object_count = schema->routineGroups().count();

    WBComponentPhysical *compo = wb::WBContextUI::get()->get_wb()->get_component<WBComponentPhysical>();

    compo->place_new_db_object(data->diagramForm, base::Point(10, 10), wb::ObjectRoutineGroup);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add";
    EXPECT_EQ(schema->routineGroups().count(), old_object_count + 1) << "group add";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count) << "figure add undo";
    EXPECT_EQ(schema->routineGroups().count(), old_object_count) << "group add undo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count) << "figure root add undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add redo";
    EXPECT_EQ(schema->routineGroups().count(), old_object_count + 1) << "group add redo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceImage) {
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_root_figure_count = data->diagram->rootLayer()->figures().count();

    // Place image will ask for a filename of the image.
    data->tester->addFileForFileDialog(data->dataDir + "/images/sakila.png");

    data->placeFigureWithTool(WB_TOOL_IMAGE);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count) << "figure add undo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count) << "figure root add undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add redo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceText) {
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_root_figure_count = data->diagram->rootLayer()->figures().count();

    data->placeFigureWithTool(WB_TOOL_NOTE);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count) << "figure add undo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count) << "figure root add undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "figure add redo";
    EXPECT_EQ(data->diagram->rootLayer()->figures().count(), old_root_figure_count + 1) << "figure root add redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceLayer) {
    size_t old_layer_count = data->diagram->layers().count();
    size_t old_root_layer_count = data->diagram->rootLayer()->subLayers().count();

    data->diagramForm->set_tool(WB_TOOL_LAYER);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, 10, 10, (mdc::EventState)0);
    data->diagramForm->handle_mouse_move(50, 50, mdc::SLeftButtonMask);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, false, 50, 50, (mdc::EventState)0);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->layers().count(), old_layer_count + 1) << "layer add";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count + 1) << "layer root add";

    data->checkUndo();
    EXPECT_EQ(data->diagram->layers().count(), old_layer_count) << "layer add undo";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count) << "layer root add undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->layers().count(), old_layer_count + 1) << "layer add redo";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count + 1) << "layer root add redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceSomethingInsideALayer) {
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_layer_count = data->diagram->layers().count();
    size_t old_root_layer_count = data->diagram->rootLayer()->subLayers().count();

    // place layer
    data->diagramForm->set_tool(WB_TOOL_LAYER);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, 10, 10, (mdc::EventState)0);
    data->diagramForm->handle_mouse_move(150, 150, (mdc::EventState)0);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, false, 150, 150, (mdc::EventState)0);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->layers().count(), old_layer_count + 1) << "layer add";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count + 1) << "layer root add";

    model_LayerRef layer(data->diagram->layers()[old_layer_count]);
    EXPECT_TRUE(layer.is_valid()) << "layer";
    EXPECT_EQ(layer->figures().count(), 0U) << "layer empty";

    // place a note inside the layer
    data->placeFigureWithTool(WB_TOOL_NOTE, 50, 50);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "note add";
    model_FigureRef figure(data->diagram->figures()[old_figure_count]);

    EXPECT_EQ(*figure->top(), 40) << "new note pos";
    EXPECT_EQ(*figure->left(), 40) << "new note pos";

    EXPECT_EQ(layer->figures().count(), 1U) << "layer contains figure";
    EXPECT_EQ(figure->layer(), layer) << "note layer";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count) << "note add undo";
    EXPECT_EQ(layer->figures().count(), 0U) << "layer contains figure undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "note add undo";
    EXPECT_EQ(layer->figures().count(), 1U) << "layer contains figure undo";

    data->checkUndo();
    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceLayerAroundSomething) {
    size_t old_figure_count = data->diagram->figures().count();
    size_t old_layer_count = data->diagram->layers().count();
    size_t old_root_layer_count = data->diagram->rootLayer()->subLayers().count();

    // place a note
    data->diagramForm->set_tool(WB_TOOL_NOTE);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, 200, 200, (mdc::EventState)0);
    data->checkOnlyOneUndoAdded();

    model_FigureRef figure(data->diagram->figures()[old_figure_count]);

    EXPECT_EQ(data->diagram->figures().count(), old_figure_count + 1) << "note add";

    // place layer around the note
    data->diagramForm->set_tool(WB_TOOL_LAYER);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, 190, 190, (mdc::EventState)0);
    data->diagramForm->handle_mouse_move(300, 300, (mdc::EventState)0);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, false, 300, 300, (mdc::EventState)0);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->layers().count(), old_layer_count + 1) << "layer add";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count + 1) << "layer root add";

    model_LayerRef layer(data->diagram->layers()[old_layer_count]);
    EXPECT_TRUE(layer.is_valid()) << "layer";
    EXPECT_EQ(figure->layer(), layer) << "note layer";

    EXPECT_EQ(layer->figures().count(), 1U) << "layer contains note only";
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains note only";

    EXPECT_EQ(data->diagram->layers().count(), old_layer_count + 1) << "layer add";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count + 1) << "layer root add";
    EXPECT_EQ(layer->figures().count(), 1U) << "layer note";
    EXPECT_EQ(layer->figures().count(), 1U) << "root layer note";

    data->checkUndo();
    EXPECT_EQ(data->diagram->layers().count(), old_layer_count) << "layer add undo";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count) << "layer root add undo";
    EXPECT_EQ(layer->figures().count(), 0U) << "layer note";

    data->checkRedo();
    EXPECT_EQ(data->diagram->layers().count(), old_layer_count + 1) << "layer add redo";
    EXPECT_EQ(data->diagram->rootLayer()->subLayers().count(), old_root_layer_count + 1) << "layer root add redo";
    EXPECT_EQ(layer->figures().count(), 1U) << "layer note";

    data->checkUndo(); // undo the layer
    data->checkUndo(); // undo the note place
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, MoveObject) {
    double x, y;
    model_FigureRef figure(data->diagram->figures()[0]);

    x = figure->left();
    y = figure->top();

    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, static_cast<int>(x + 5), static_cast<int>(y + 5),
                                           (mdc::EventState)0);
    data->diagramForm->handle_mouse_move(50, 50, mdc::SLeftButtonMask);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, false, 50, 50, mdc::SLeftButtonMask);
    data->checkOnlyOneUndoAdded();

    EXPECT_NE(*figure->left(), x) << "object moved";

    data->checkUndo();
    EXPECT_EQ(*figure->left(), x) << "move undo";

    data->checkRedo();
    EXPECT_NE(*figure->left(), x) << "move redo";

    data->checkUndo();
  }

  //--------------------------------------------------------------------------------------------------------------------

  TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, MoveIntoAndOutOfLayer) {
    double x, y;
    model_FigureRef figure(data->diagram->figures()[0]);
    model_LayerRef layer(data->diagram->layers()[0]);

    EXPECT_NE(layer, data->diagram->rootLayer()) << "layer is not root";

    x = figure->left();
    y = figure->top();

    EXPECT_EQ(figure->layer(), data->diagram->rootLayer()) << "object is in root";
    EXPECT_EQ(layer->figures().count(), 0U) << "layer begins empty";

    // move object into layer
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, static_cast<int>(x + 5), static_cast<int>(y + 5),
                                           (mdc::EventState)0);
    data->diagramForm->handle_mouse_move(150, 400, mdc::SLeftButtonMask);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, false, 150, 400, mdc::SLeftButtonMask);
    data->checkOnlyOneUndoAdded();

    EXPECT_NE(*figure->left(), x) << "object moved";
    EXPECT_EQ(figure->layer(), layer) << "object moved into layer";
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains object";
    EXPECT_EQ(data->diagram->rootLayer()->figures()->get_index(figure), BaseListRef::npos) << "object not in root";

    data->checkUndo();
    EXPECT_EQ(*figure->left(), x) << "move undo";
    EXPECT_EQ(figure->layer(), data->diagram->rootLayer()) << "object moved into layer undo";
    EXPECT_EQ(layer->figures().count(), 0U) << "layer empty on undo";
    EXPECT_EQ(layer->figures().get_index(figure), BaseListRef::npos) << "layer does not contain object";
    EXPECT_NE(data->diagram->rootLayer()->figures()->get_index(figure), BaseListRef::npos) << "object in root";

    data->checkRedo();
    EXPECT_NE(*figure->left(), x) << "move redo";
    EXPECT_EQ(figure->layer(), layer) << "object moved into layer redo";
    EXPECT_EQ(layer->figures().count(), 1U) << "layer contains stuff after redo";
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains object";
    EXPECT_EQ(data->diagram->rootLayer()->figures()->get_index(figure), BaseListRef::npos) << "object not in root";

    // Move object out of layer.
    double new_x = figure->left();

    double mouse_x = figure->left() + layer->left();
    double mouse_y = figure->top() + layer->top();

    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, (int)(mouse_x + 5), (int)(mouse_y + 5), (mdc::EventState)0);
    data->diagramForm->handle_mouse_move(10, 10, mdc::SLeftButtonMask);
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, false, 10, 10, mdc::SLeftButtonMask);
    data->checkOnlyOneUndoAdded();

    EXPECT_NE(*figure->left(), new_x) << "object moved";
    EXPECT_EQ(figure->layer(), data->diagram->rootLayer()) << "object moved out of layer";
    EXPECT_EQ(layer->figures().get_index(figure), BaseListRef::npos) << "layer is empty";
    EXPECT_NE(data->diagram->rootLayer()->figures().get_index(figure), BaseListRef::npos) << "object in root layer";

    data->checkUndo();

    EXPECT_EQ(*figure->left(), new_x) << "object moved undo";
    EXPECT_EQ(figure->layer(), layer) << "object moved out of layer undo";
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer is not empty";
    EXPECT_EQ(data->diagram->rootLayer()->figures().get_index(figure), BaseListRef::npos) << "object not in root layer";

    data->checkRedo();
    EXPECT_NE(*figure->left(), new_x) << "object moved";
    EXPECT_EQ(figure->layer(), data->diagram->rootLayer()) << "object moved out of layer";
    EXPECT_EQ(layer->figures().get_index(figure), BaseListRef::npos) << "layer is empty";
    EXPECT_NE(data->diagram->rootLayer()->figures().get_index(figure), BaseListRef::npos) << "object in root layer";

    // undo both operations
    data->checkUndo();
    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, MoveLayer) {
    GTEST_SKIP() << "not implemented";
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, MoveLlayerUnderObjectToCaptureIt) {
    GTEST_SKIP() << "not implemented";
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, ResizeLayerToEatAFigure) {
    model_LayerRef layer(data->diagram->layers()[0]);

    data->placeFigureWithTool(WB_TOOL_NOTE, 580, 480);
    data->checkOnlyOneUndoAdded();

    data->diagram->unselectAll();

    model_FigureRef figure(find_named_object_in_list(data->diagram->figures(), "text1"));

    // resize the layer to cover figure
    data->resizeObject(data->diagramForm, layer, 800, 800);

    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(*layer->width(), 800) << "layer resized properly";
    EXPECT_EQ(*layer->height(), 800) << "layer resized properly";

    // At this point the layer should have captured the text figure, but there's a long term bug
    // pending where figure <-> layer relationship is only updated when a figure was dragged
    // (regardless which of both was dragged, layer or child figure).
    // TODO: For now we drag the layer a bit to make this work. Needs to be addressed sooner or later.
    data->dragObject(data->diagramForm, layer, 10, 10);
    data->checkOnlyOneUndoAdded();

    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains object";
    EXPECT_EQ(data->diagram->rootLayer()->figures().get_index(figure), BaseListRef::npos) << "object not in root";
    EXPECT_EQ(figure->layer(), layer) << "object layer changed";

    data->checkUndo();
    EXPECT_EQ(layer->figures().get_index(figure), BaseListRef::npos) << "layer not contains object";
    EXPECT_NE(data->diagram->rootLayer()->figures().get_index(figure), BaseListRef::npos) << "object in root";
    EXPECT_NE(figure->layer(), layer) << "object layer changed";

    data->checkRedo();
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains object";
    EXPECT_EQ(data->diagram->rootLayer()->figures().get_index(figure), BaseListRef::npos) << "object not in root";
    EXPECT_EQ(figure->layer(), layer) << "object layer changed";

    data->checkUndo(); // Layer dragging.
    data->checkUndo(); // Layer resize.
    data->checkUndo(); // Note add.
}

  //--------------------------------------------------------------------------------------------------------------------

  //  XXX: for now disabled as there's a bug which must be fixed first (but cannot right now).
  //       Internal bug number #268.
  // TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, ResizeTable) // Resize Table
  // {
  //   model_FigureRef figure(find_named_object_in_list(data->diagram->figures(), "table1"));
  //
  //   EXPECT_TRUE(figure.is_valid()) << "table found";
  //
  //   double w,h;
  //   w= figure->width();
  //   h= figure->height();
  //
  //   // resize the figure
  //   resize_object(data->diagramForm, figure, 150, 200);
  //   data->checkOnlyOneUndoAdded();
  //
  //   EXPECT_EQ(*figure->width(), 150) << "Table width is wrong";
  //   EXPECT_EQ(*figure->height(), 200) << "Table height is wrong";
  //
  //   data->checkUndo();
  //   EXPECT_EQ(*figure->width(), w) << "Table width is wrong";
  //   EXPECT_EQ(*figure->height(), h) << "Table height is wrong";
  //
  //   data->checkUndo();
  //   EXPECT_EQ(*figure->width(), 150) << "Table width is wrong";
  //   EXPECT_EQ(*figure->height(), 200) << "Table height is wrong";
  //   data->checkUndo();
  // }

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteTable) {
    model_FigureRef figure(find_named_object_in_list(data->diagram->figures(), "table1"));

    EXPECT_TRUE(figure.is_valid()) << "table found";

    // delete the figure
    mforms::stub::UtilitiesWrapper::set_message_callback(message_ok_callback);
    wb::WBContextUI::get()->get_wb()->get_model_context()->delete_object(figure);
    data->checkOnlyOneUndoAdded();

    EXPECT_FALSE(find_named_object_in_list(data->diagram->figures(), "table1").is_valid()) << "table delete";
    EXPECT_FALSE(find_named_object_in_list(data->diagram->rootLayer()->figures(), "table1").is_valid()) << "table delete";

    data->checkUndo();
    EXPECT_TRUE(find_named_object_in_list(data->diagram->figures(), "table1").is_valid()) << "table delete undo";
    EXPECT_TRUE(find_named_object_in_list(data->diagram->rootLayer()->figures(), "table1").is_valid()) << "table delete undo";

    data->checkRedo();
    EXPECT_FALSE(find_named_object_in_list(data->diagram->figures(), "table1").is_valid()) << "table delete redo";
    EXPECT_FALSE(find_named_object_in_list(data->diagram->rootLayer()->figures(), "table1").is_valid()) << "table delete redo";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteLayerWithStuffInside) {
    GTEST_SKIP() << "not implemented";
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceWithDragDrop) {
    db_TableRef table(find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table3"));

    EXPECT_TRUE(table.is_valid()) << "table found";

    std::list<GrtObjectRef> list;
    list.push_back(table);
    data->diagramForm->perform_drop(10, 10, WB_DBOBJECT_DRAG_TYPE, list);
    data->checkOnlyOneUndoAdded();

    EXPECT_TRUE(find_named_object_in_list(data->diagram->figures(), "table3").is_valid()) << "table figure added";
    model_FigureRef figure(find_named_object_in_list(data->diagram->figures(), "table3"));
    EXPECT_TRUE(figure->layer().is_valid()) << "figure has proper layer set";
    EXPECT_EQ(figure->layer(), data->diagram->rootLayer()) << "figure has proper layer set";
    EXPECT_EQ(figure->owner(), data->diagram) << "figure has proper owner set";

    data->checkUndo();
    EXPECT_FALSE(find_named_object_in_list(data->diagram->figures(), "table3").is_valid()) << "table figure added undo";

    data->checkRedo();
    EXPECT_TRUE(find_named_object_in_list(data->diagram->figures(), "table3").is_valid()) << "table figure added";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, PlaceWithDragDropOnALayer) {
    db_TableRef table(find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table3"));

    EXPECT_TRUE(table.is_valid()) << "table found";

    std::list<GrtObjectRef> list;
    list.push_back(table);
    data->diagramForm->perform_drop(200, 500, WB_DBOBJECT_DRAG_TYPE, list);
    data->checkOnlyOneUndoAdded();

    model_LayerRef layer(data->diagram->layers()[0]);
    model_FigureRef figure;

    figure = find_named_object_in_list(data->diagram->figures(), "table3");

    EXPECT_TRUE(figure.is_valid()) << "table figure added";
    EXPECT_EQ(figure->layer(), layer) << "table in layer";
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains table";

    data->checkUndo();
    EXPECT_FALSE(find_named_object_in_list(data->diagram->figures(), "table3").is_valid()) << "table figure added undo";
    EXPECT_EQ(layer->figures().get_index(figure), BaseListRef::npos) << "layer not contains table";

    data->checkRedo();
    EXPECT_TRUE(find_named_object_in_list(data->diagram->figures(), "table3").is_valid()) << "table figure added";
    EXPECT_EQ(figure->layer(), layer) << "table in layer";
    EXPECT_NE(layer->figures().get_index(figure), BaseListRef::npos) << "layer contains table";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, CreateRelationshipInDiagram) {
    data->diagramForm->set_tool(WB_TOOL_PREL1n);

    model_FigureRef table1(find_named_object_in_list(data->diagram->figures(), "table1"));
    model_FigureRef table2(find_named_object_in_list(data->diagram->figures(), "table2"));

    EXPECT_TRUE(table1.is_valid() && table2.is_valid()) << "found tables";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    // click table1
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, static_cast<int>(*table1->left() + 20),
                                           static_cast<int>(*table1->top() + 20), (mdc::EventState)0);
    // click table2
    data->diagramForm->handle_mouse_button(mdc::ButtonLeft, true, static_cast<int>(*table2->left() + 20),
                                           static_cast<int>(*table2->top() + 20), (mdc::EventState)0);

    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 2U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 2U) << "rel count after redo";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, CreateRelationshipIndirectlyWithFK) {
    db_TableRef table1(find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table1"));
    db_TableRef table2(find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table2"));

    EXPECT_TRUE(table1.is_valid() && table2.is_valid()) << "table valid";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    db_ForeignKeyRef fk;

    fk = bec::TableHelper::create_foreign_key_to_table(table1, table2, true, true, true, false,
      data->tester->getRdbms(), DictRef(true), DictRef(true));
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 2U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 2U) << "rel count after redo";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, CreateRelationshipDroppingTableWithFK) {
    db_TableRef table(
      find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table_with_fk")
    );

    EXPECT_TRUE(table.is_valid()) << "table found";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    std::list<GrtObjectRef> list;
    list.push_back(table);
    data->diagramForm->perform_drop(200, 500, WB_DBOBJECT_DRAG_TYPE, list);
    data->checkOnlyOneUndoAdded();

    model_FigureRef figure;

    figure = find_named_object_in_list(data->diagram->figures(), "table_with_fk");

    EXPECT_TRUE(figure.is_valid()) << "table figure added";
    EXPECT_EQ(data->diagram->connections().count(), 2U) << "rel count";

    data->checkUndo();
    EXPECT_FALSE(find_named_object_in_list(data->diagram->figures(), "table_with_fk").is_valid()) << "table figure added undo";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    data->checkRedo();
    EXPECT_TRUE(find_named_object_in_list(data->diagram->figures(), "table_with_fk").is_valid()) << "table figure added redo";
    EXPECT_EQ(data->diagram->connections().count(), 2U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, CreateRelationshipDroppingTableReferencedByFK) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";
    EXPECT_FALSE(find_named_object_in_list(data->diagram->figures(), "table_with_fk").is_valid()) << "Table already in diagram";

    EXPECT_EQ(data->diagram->figures().count(), 5U) << "Wrong figure count";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "Wrong relationship count";

    // Now drag/drop the table to the diagram.
    std::list<GrtObjectRef> list;
    list.push_back(find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table_with_fk"));
    data->diagramForm->perform_drop(10, 10, WB_DBOBJECT_DRAG_TYPE, list);
    data->checkOnlyOneUndoAdded();

    EXPECT_TRUE(find_named_object_in_list(data->diagram->figures(), "table_with_fk").is_valid()) << "Table not found";
    EXPECT_EQ(data->diagram->connections().count(), 2U) << "Wrong relationship count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->figures().count(), 5U) << "Wrong figure count";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "Wrong relationship count";

    data->checkRedo();
    EXPECT_EQ(data->diagram->figures().count(), 6U) << "Wrong figure count";
    EXPECT_EQ(data->diagram->connections().count(), 2U) << "Wrong relationship count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipInDiagram) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(conn);
    EXPECT_EQ(data->diagram->selection().count(), 1U) << "selection";

    // Message callback set in case 32.
    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipIndirectlyWithFK) {
    db_TableRef table2(find_named_object_in_list(data->tester->getPmodel()->catalog()->schemata()[0]->tables(), "table2"));

    EXPECT_TRUE(table2.is_valid()) << "table valid";
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";
    EXPECT_EQ(table2->foreignKeys().count(), 1U) << "fk count";

    db_ColumnRef column(table2->columns()[1]);

    table2->removeColumn(column);
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(table2->foreignKeys().count(), 0U) << "fk count";
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";
    EXPECT_EQ(table2->foreignKeys().count(), 1U) << "fk count";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count after redo";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipDeletedTable) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(find_named_object_in_list(data->diagram->figures(), "table2"));
    EXPECT_EQ(data->diagram->selection().count(), 1U) << "selection";

    // Message callback set in case 32.
    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipDeletedTableFigureOnly) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(find_named_object_in_list(data->diagram->figures(), "table2"));
    EXPECT_EQ(data->diagram->selection().count(), 1U) << "selection";

    // Keep db objects
    mforms::stub::UtilitiesWrapper::set_message_callback(message_cancel_callback);
    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipDeletedReferencedTable) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(find_named_object_in_list(data->diagram->figures(), "table1"));
    EXPECT_EQ(data->diagram->selection().count(), 1U) << "selection";

    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipAndRefTable) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(conn->endFigure());
    data->diagram->selectObject(conn);
    EXPECT_EQ(data->diagram->selection().count(), 2U) << "selection";

    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipAndTable) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(conn);
    data->diagram->selectObject(conn->startFigure());
    EXPECT_EQ(data->diagram->selection().count(), 2U) << "selection";

    mforms::stub::UtilitiesWrapper::set_message_callback(message_ok_callback);
    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}

  //--------------------------------------------------------------------------------------------------------------------

TEST_F(UndoRedoForDiagramActionsInSqlStudioTest, DeleteRelationshipAndBothTables) {
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count";

    model_ConnectionRef conn(data->diagram->connections()[0]);

    data->diagram->unselectAll();

    data->diagram->selectObject(conn->startFigure());
    data->diagram->selectObject(conn);
    data->diagram->selectObject(conn->endFigure());
    EXPECT_EQ(data->diagram->selection().count(), 3U) << "selection";

    mforms::stub::UtilitiesWrapper::set_message_callback(message_ok_callback);
    data->diagramForm->delete_selection();
    data->checkOnlyOneUndoAdded();

    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
    EXPECT_EQ(data->diagram->connections().count(), 1U) << "rel count after undo";

    data->checkRedo();
    EXPECT_EQ(data->diagram->connections().count(), 0U) << "rel count";

    data->checkUndo();
}
}
