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
import 'package:rive/rive.dart' hide LinearGradient;
import 'package:auto_size_text/auto_size_text.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:provider/provider.dart';
import 'datatable_model.dart';
export 'datatable_model.dart';

class DatatableWidget extends StatefulWidget {
  const DatatableWidget({super.key});

  @override
  State<DatatableWidget> createState() => _DatatableWidgetState();
}

class _DatatableWidgetState extends State<DatatableWidget> {
  late DatatableModel _model;

  final scaffoldKey = GlobalKey<ScaffoldState>();

  @override
  void initState() {
    super.initState();
    _model = createModel(context, () => DatatableModel());

    // On page load action.
    SchedulerBinding.instance.addPostFrameCallback((_) async {
      _model.tapwateractionvar = await TapwaterdbTable().queryRows(
        queryFn: (q) => q.order('date', ascending: true),
      );
      await Future.wait([
        Future(() async {
          _model.startedActionVar = await StartTimeTable().queryRows(
            queryFn: (q) => q.eq(
              'arrived',
              true,
            ),
          );
          setState(() {
            _model.started = _model.started;
          });
        }),
      ]);
      setState(() {
        _model.listOfTapWater =
            _model.tapwateractionvar!.toList().cast<TapwaterdbRow>();
      });
    });
  }

  @override
  void dispose() {
    _model.dispose();

    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () => _model.unfocusNode.canRequestFocus
          ? FocusScope.of(context).requestFocus(_model.unfocusNode)
          : FocusScope.of(context).unfocus(),
      child: Scaffold(
        key: scaffoldKey,
        backgroundColor: FlutterFlowTheme.of(context).primaryBackground,
        appBar: PreferredSize(
          preferredSize:
              Size.fromHeight(MediaQuery.sizeOf(context).height * 0.3),
          child: AppBar(
            backgroundColor: FlutterFlowTheme.of(context).primary,
            automaticallyImplyLeading: false,
            title: Stack(
              alignment: AlignmentDirectional(0.0, 0.0),
              children: [
                Align(
                  alignment: AlignmentDirectional(0.0, -1.0),
                  child: Container(
                    width: MediaQuery.sizeOf(context).width * 1.046,
                    height: MediaQuery.sizeOf(context).height * 0.25,
                    decoration: BoxDecoration(),
                    alignment: AlignmentDirectional(0.0, -1.0),
                    child: Visibility(
                      visible: _model.startedActionVar != null &&
                          (_model.startedActionVar)!.isNotEmpty,
                      child: Container(
                        width: 145.0,
                        child: RiveAnimation.asset(
                          'assets/rive_animations/fish_in_a_bowl.riv',
                          artboard: 'WaterWaves',
                          fit: BoxFit.none,
                          controllers: _model.riveAnimationControllers,
                        ),
                      ),
                    ),
                  ),
                ),
                if (!(_model.startedActionVar != null &&
                    (_model.startedActionVar)!.isNotEmpty))
                  Text(
                    'Tap Water History',
                    textAlign: TextAlign.start,
                    style: FlutterFlowTheme.of(context).titleLarge.override(
                          fontFamily: 'Outfit',
                          color: Color(0xFFE6E6E7),
                          fontSize: 30.0,
                          letterSpacing: 0.0,
                        ),
                  ),
                Align(
                  alignment: AlignmentDirectional(0.0, 1.0),
                  child: InkWell(
                    splashColor: Colors.transparent,
                    focusColor: Colors.transparent,
                    hoverColor: Colors.transparent,
                    highlightColor: Colors.transparent,
                    onTap: () async {
                      _model.sin = await TapwaterdbTable().queryRows(
                        queryFn: (q) => q.order('date', ascending: true),
                      );

                      setState(() {});
                    },
                    child: Container(
                      height: MediaQuery.sizeOf(context).height * 0.3,
                      child: Stack(
                        children: [],
                      ),
                    ),
                  ),
                ),
              ],
            ),
            actions: [],
            centerTitle: false,
            toolbarHeight: MediaQuery.sizeOf(context).height * 0.4,
            elevation: 1.0,
          ),
        ),
        body: SafeArea(
          top: true,
          child: Stack(
            children: [
              Builder(
                builder: (context) {
                  final tapwaterchildren = _model.listOfTapWater.toList();
                  return FlutterFlowDataTable<TapwaterdbRow>(
                    controller: _model.paginatedDataTableController,
                    data: tapwaterchildren,
                    columnsBuilder: (onSortChanged) => [
                      DataColumn2(
                        label: DefaultTextStyle.merge(
                          softWrap: true,
                          child: Row(
                            mainAxisSize: MainAxisSize.max,
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Flexible(
                                child: AutoSizeText(
                                  'Date',
                                  textAlign: TextAlign.justify,
                                  style: FlutterFlowTheme.of(context)
                                      .labelLarge
                                      .override(
                                        fontFamily: 'Readex Pro',
                                        color: Color(0xFFE6EAED),
                                        fontSize:
                                            MediaQuery.sizeOf(context).width <
                                                    kBreakpointSmall
                                                ? 15.0
                                                : 16.0,
                                        letterSpacing: 0.0,
                                      ),
                                ),
                              ),
                            ],
                          ),
                        ),
                        fixedWidth: MediaQuery.sizeOf(context).width * 0.3,
                        onSort: onSortChanged,
                      ),
                      DataColumn2(
                        label: DefaultTextStyle.merge(
                          softWrap: true,
                          child: Row(
                            mainAxisSize: MainAxisSize.max,
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Expanded(
                                flex: 2,
                                child: AutoSizeText(
                                  'Start',
                                  style: FlutterFlowTheme.of(context)
                                      .labelLarge
                                      .override(
                                        fontFamily: 'Readex Pro',
                                        color: Color(0xFFEBEFF1),
                                        fontSize:
                                            MediaQuery.sizeOf(context).width <
                                                    kBreakpointSmall
                                                ? 15.0
                                                : 16.0,
                                        letterSpacing: 0.0,
                                      ),
                                ),
                              ),
                            ],
                          ),
                        ),
                        onSort: onSortChanged,
                      ),
                      DataColumn2(
                        label: DefaultTextStyle.merge(
                          softWrap: true,
                          child: Row(
                            mainAxisSize: MainAxisSize.max,
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Expanded(
                                flex: 2,
                                child: AutoSizeText(
                                  'End',
                                  style: FlutterFlowTheme.of(context)
                                      .labelLarge
                                      .override(
                                        fontFamily: 'Readex Pro',
                                        color: Color(0xFFEDEFF1),
                                        fontSize:
                                            MediaQuery.sizeOf(context).width <
                                                    kBreakpointSmall
                                                ? 15.0
                                                : 16.0,
                                        letterSpacing: 0.0,
                                      ),
                                ),
                              ),
                            ],
                          ),
                        ),
                        onSort: onSortChanged,
                      ),
                      DataColumn2(
                        label: DefaultTextStyle.merge(
                          softWrap: true,
                          child: Row(
                            mainAxisSize: MainAxisSize.max,
                            mainAxisAlignment: MainAxisAlignment.end,
                            children: [
                              Flexible(
                                flex: 1,
                                child: AutoSizeText(
                                  'Duration',
                                  textAlign: TextAlign.center,
                                  style: FlutterFlowTheme.of(context)
                                      .labelLarge
                                      .override(
                                        fontFamily: 'Readex Pro',
                                        color: Color(0xFFF0F2F3),
                                        fontSize:
                                            MediaQuery.sizeOf(context).width <
                                                    kBreakpointSmall
                                                ? 15.0
                                                : 16.0,
                                        letterSpacing: 0.0,
                                      ),
                                ),
                              ),
                            ],
                          ),
                        ),
                        fixedWidth: MediaQuery.sizeOf(context).width * 0.23,
                        onSort: onSortChanged,
                      ),
                    ],
                    dataRowBuilder: (tapwaterchildrenItem,
                            tapwaterchildrenIndex, selected, onSelectChanged) =>
                        DataRow(
                      color: MaterialStateProperty.all(
                        tapwaterchildrenIndex % 2 == 0
                            ? FlutterFlowTheme.of(context).secondaryBackground
                            : FlutterFlowTheme.of(context).primaryBackground,
                      ),
                      cells: [
                        Align(
                          alignment: AlignmentDirectional(0.0, 0.0),
                          child: Row(
                            mainAxisSize: MainAxisSize.max,
                            children: [
                              Flexible(
                                child: AutoSizeText(
                                  valueOrDefault<String>(
                                    tapwaterchildrenItem.date,
                                    'NA',
                                  ),
                                  style: FlutterFlowTheme.of(context)
                                      .bodyMedium
                                      .override(
                                        fontFamily: 'Readex Pro',
                                        fontSize:
                                            MediaQuery.sizeOf(context).width <
                                                    kBreakpointSmall
                                                ? 13.0
                                                : 13.0,
                                        letterSpacing: 0.0,
                                      ),
                                ),
                              ),
                            ],
                          ),
                        ),
                        Row(
                          mainAxisSize: MainAxisSize.max,
                          mainAxisAlignment: MainAxisAlignment.start,
                          children: [
                            Expanded(
                              flex: 2,
                              child: Text(
                                valueOrDefault<String>(
                                  tapwaterchildrenItem.startTime,
                                  'NA',
                                ),
                                style: FlutterFlowTheme.of(context)
                                    .bodyMedium
                                    .override(
                                      fontFamily: 'Readex Pro',
                                      fontSize:
                                          MediaQuery.sizeOf(context).width <
                                                  kBreakpointSmall
                                              ? 13.0
                                              : 13.0,
                                      letterSpacing: 0.0,
                                    ),
                              ),
                            ),
                          ].divide(SizedBox(width: 5.0)),
                        ),
                        Row(
                          mainAxisSize: MainAxisSize.max,
                          mainAxisAlignment: MainAxisAlignment.start,
                          children: [
                            Expanded(
                              flex: 2,
                              child: Text(
                                valueOrDefault<String>(
                                  tapwaterchildrenItem.endTime,
                                  'NA',
                                ),
                                style: FlutterFlowTheme.of(context)
                                    .bodyMedium
                                    .override(
                                      fontFamily: 'Readex Pro',
                                      fontSize:
                                          MediaQuery.sizeOf(context).width <
                                                  kBreakpointSmall
                                              ? 13.0
                                              : 13.0,
                                      letterSpacing: 0.0,
                                    ),
                              ),
                            ),
                          ],
                        ),
                        Align(
                          alignment: AlignmentDirectional(0.0, 0.0),
                          child: Row(
                            mainAxisSize: MainAxisSize.max,
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Flexible(
                                flex: 1,
                                child: Text(
                                  valueOrDefault<String>(
                                    tapwaterchildrenItem.duration?.toString(),
                                    'NA',
                                  ),
                                  style: FlutterFlowTheme.of(context)
                                      .bodyMedium
                                      .override(
                                        fontFamily: 'Readex Pro',
                                        fontSize:
                                            MediaQuery.sizeOf(context).width <
                                                    kBreakpointSmall
                                                ? 13.0
                                                : 13.0,
                                        letterSpacing: 0.0,
                                      ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ].map((c) => DataCell(c)).toList(),
                    ),
                    onPageChanged: (currentRowIndex) async {
                      _model.tapwaterActionVar =
                          await TapwaterdbTable().queryRows(
                        queryFn: (q) => q.order('id', ascending: true),
                      );
                      setState(() {});

                      setState(() {});
                    },
                    onSortChanged: (columnIndex, ascending) async {
                      setState(() {
                        _model.listOfTapWater = functions
                            .sortByDateFunc(_model.listOfTapWater.toList(),
                                columnIndex, ascending)
                            .toList()
                            .cast<TapwaterdbRow>();
                      });
                    },
                    paginated: true,
                    selectable: false,
                    hidePaginator: false,
                    showFirstLastButtons: false,
                    height: MediaQuery.sizeOf(context).height * 0.5,
                    headingRowHeight: 60.0,
                    dataRowHeight: 60.0,
                    columnSpacing: 8.0,
                    headingRowColor: FlutterFlowTheme.of(context).primary,
                    sortIconColor: FlutterFlowTheme.of(context).primary,
                    borderRadius: BorderRadius.circular(8.0),
                    addHorizontalDivider: true,
                    addTopAndBottomDivider: false,
                    hideDefaultHorizontalDivider: true,
                    horizontalDividerColor:
                        FlutterFlowTheme.of(context).secondaryBackground,
                    horizontalDividerThickness: 1.0,
                    addVerticalDivider: false,
                  );
                },
              ),
              Stack(
                children: [],
              ),
              Align(
                alignment: AlignmentDirectional(0.79, 0.82),
                child: FFButtonWidget(
                  onPressed: () async {
                    context.pushNamed(
                      'datatable',
                      extra: <String, dynamic>{
                        kTransitionInfoKey: TransitionInfo(
                          hasTransition: true,
                          transitionType: PageTransitionType.fade,
                          duration: Duration(milliseconds: 0),
                        ),
                      },
                    );
                  },
                  text: 'Refresh',
                  options: FFButtonOptions(
                    height: 40.0,
                    padding:
                        EdgeInsetsDirectional.fromSTEB(24.0, 0.0, 24.0, 0.0),
                    iconPadding:
                        EdgeInsetsDirectional.fromSTEB(0.0, 0.0, 0.0, 0.0),
                    color: Color(0xFF7EC1FF),
                    textStyle:
                        FlutterFlowTheme.of(context).titleMedium.override(
                              fontFamily: 'Readex Pro',
                              letterSpacing: 0.0,
                            ),
                    elevation: 3.0,
                    borderSide: BorderSide(
                      color: Color(0xFF60A8EE),
                      width: 1.0,
                    ),
                    borderRadius: BorderRadius.circular(8.0),
                  ),
                ),
              ),
              if (_model.startedActionVar != null &&
                  (_model.startedActionVar)!.isNotEmpty)
                Align(
                  alignment: AlignmentDirectional(-0.8, 0.82),
                  child: FFButtonWidget(
                    onPressed: () async {
                      _model.apiResultwh3 = await TurnoffalarmCall.call();
                      _model.instantTimer = InstantTimer.periodic(
                        duration: Duration(milliseconds: 2000),
                        callback: (timer) async {
                          unawaited(
                            () async {
                              _model.alarmoffreferesh =
                                  await StartTimeTable().queryRows(
                                queryFn: (q) => q,
                              );
                            }(),
                          );
                          setState(() {
                            _model.started = _model.started;
                          });
                        },
                        startImmediately: true,
                      );

                      setState(() {});
                    },
                    text: 'Alarm off',
                    options: FFButtonOptions(
                      height: 40.0,
                      padding:
                          EdgeInsetsDirectional.fromSTEB(24.0, 0.0, 24.0, 0.0),
                      iconPadding:
                          EdgeInsetsDirectional.fromSTEB(0.0, 0.0, 0.0, 0.0),
                      color: Color(0xFF7EC1FF),
                      textStyle:
                          FlutterFlowTheme.of(context).titleMedium.override(
                                fontFamily: 'Readex Pro',
                                letterSpacing: 0.0,
                              ),
                      elevation: 3.0,
                      borderSide: BorderSide(
                        color: Color(0xFF60A8EE),
                        width: 1.0,
                      ),
                      borderRadius: BorderRadius.circular(8.0),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
