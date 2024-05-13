import '/backend/api_requests/api_calls.dart';
import '/backend/supabase/supabase.dart';
import '/flutter_flow/flutter_flow_data_table.dart';
import '/flutter_flow/flutter_flow_rive_controller.dart';
import '/flutter_flow/flutter_flow_theme.dart';
import '/flutter_flow/flutter_flow_util.dart';
import '/flutter_flow/flutter_flow_widgets.dart';
import '/flutter_flow/instant_timer.dart';
import 'dart:async';
import '/flutter_flow/custom_functions.dart' as functions;
import 'datatable_widget.dart' show DatatableWidget;
import 'package:rive/rive.dart' hide LinearGradient;
import 'package:auto_size_text/auto_size_text.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:provider/provider.dart';

class DatatableModel extends FlutterFlowModel<DatatableWidget> {
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

  StartTimeRow? started;

  ///  State fields for stateful widgets in this page.

  final unfocusNode = FocusNode();
  // Stores action output result for [Backend Call - Query Rows] action in datatable widget.
  List<TapwaterdbRow>? tapwateractionvar;
  // Stores action output result for [Backend Call - Query Rows] action in datatable widget.
  List<StartTimeRow>? startedActionVar;
  // State field(s) for RiveAnimation widget.
  final riveAnimationAnimationsList = [
    'Joy 3 Effects',
    'Pop - Active',
  ];
  List<FlutterFlowRiveController> riveAnimationControllers = [];
  // Stores action output result for [Backend Call - Query Rows] action in Stack widget.
  List<TapwaterdbRow>? sin;
  // State field(s) for PaginatedDataTable widget.
  final paginatedDataTableController =
      FlutterFlowDataTableController<TapwaterdbRow>();
  // Stores action output result for [Backend Call - Query Rows] action in PaginatedDataTable widget.
  List<TapwaterdbRow>? tapwaterActionVar;
  // Stores action output result for [Backend Call - API (turnoffalarm)] action in Button widget.
  ApiCallResponse? apiResultwh3;
  InstantTimer? instantTimer;
  // Stores action output result for [Backend Call - Query Rows] action in Button widget.
  List<StartTimeRow>? alarmoffreferesh;

  @override
  void initState(BuildContext context) {
    riveAnimationAnimationsList.forEach((name) {
      riveAnimationControllers.add(FlutterFlowRiveController(
        name,
      ));
    });
  }

  @override
  void dispose() {
    unfocusNode.dispose();
    instantTimer?.cancel();
  }
}
