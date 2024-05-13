import '/backend/supabase/supabase.dart';
import '/flutter_flow/flutter_flow_charts.dart';
import '/flutter_flow/flutter_flow_icon_button.dart';
import '/flutter_flow/flutter_flow_theme.dart';
import '/flutter_flow/flutter_flow_util.dart';
import '/flutter_flow/flutter_flow_widgets.dart';
import 'graph_widget.dart' show GraphWidget;
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:provider/provider.dart';

class GraphModel extends FlutterFlowModel<GraphWidget> {
  ///  Local state fields for this page.

  List<TapwaterdbRow> listOfTapWater = [];
  void addToListOfTapWater(TapwaterdbRow item) => listOfTapWater.add(item);
  void removeFromListOfTapWater(TapwaterdbRow item) =>
      listOfTapWater.remove(item);
  void removeAtIndexFromListOfTapWater(int index) =>
      listOfTapWater.removeAt(index);
  void insertAtIndexInListOfTapWater(int index, TapwaterdbRow item) =>
      listOfTapWater.insert(index, item);
  void updateListOfTapWaterAtIndex(
          int index, Function(TapwaterdbRow) updateFn) =>
      listOfTapWater[index] = updateFn(listOfTapWater[index]);

  ///  State fields for stateful widgets in this page.

  final unfocusNode = FocusNode();
  // Stores action output result for [Backend Call - Query Rows] action in graph widget.
  List<TapwaterdbRow>? tapwateractionvar;

  @override
  void initState(BuildContext context) {}

  @override
  void dispose() {
    unfocusNode.dispose();
  }
}
