import 'dart:async';

import 'package:collection/collection.dart';

import '/backend/schema/util/firestore_util.dart';
import '/backend/schema/util/schema_util.dart';

import 'index.dart';
import '/flutter_flow/flutter_flow_util.dart';

class TapwaterdbRecord extends FirestoreRecord {
  TapwaterdbRecord._(
    DocumentReference reference,
    Map<String, dynamic> data,
  ) : super(reference, data) {
    _initializeFields();
  }

  // "date" field.
  String? _date;
  String get date => _date ?? '';
  bool hasDate() => _date != null;

  // "start_time" field.
  String? _startTime;
  String get startTime => _startTime ?? '';
  bool hasStartTime() => _startTime != null;

  // "end_time" field.
  String? _endTime;
  String get endTime => _endTime ?? '';
  bool hasEndTime() => _endTime != null;

  // "duration" field.
  String? _duration;
  String get duration => _duration ?? '';
  bool hasDuration() => _duration != null;

  void _initializeFields() {
    _date = snapshotData['date'] as String?;
    _startTime = snapshotData['start_time'] as String?;
    _endTime = snapshotData['end_time'] as String?;
    _duration = snapshotData['duration'] as String?;
  }

  static CollectionReference get collection => FirebaseFirestore.instanceFor(
          app: Firebase.app(),
          databaseURL:
              'https://firestore.googleapis.com/v1/projects/tapwater-1cd03/databases/(default)/documents')
      .collection('tapwaterdb');

  static Stream<TapwaterdbRecord> getDocument(DocumentReference ref) =>
      ref.snapshots().map((s) => TapwaterdbRecord.fromSnapshot(s));

  static Future<TapwaterdbRecord> getDocumentOnce(DocumentReference ref) =>
      ref.get().then((s) => TapwaterdbRecord.fromSnapshot(s));

  static TapwaterdbRecord fromSnapshot(DocumentSnapshot snapshot) =>
      TapwaterdbRecord._(
        snapshot.reference,
        mapFromFirestore(snapshot.data() as Map<String, dynamic>),
      );

  static TapwaterdbRecord getDocumentFromData(
    Map<String, dynamic> data,
    DocumentReference reference,
  ) =>
      TapwaterdbRecord._(reference, mapFromFirestore(data));

  @override
  String toString() =>
      'TapwaterdbRecord(reference: ${reference.path}, data: $snapshotData)';

  @override
  int get hashCode => reference.path.hashCode;

  @override
  bool operator ==(other) =>
      other is TapwaterdbRecord &&
      reference.path.hashCode == other.reference.path.hashCode;
}

Map<String, dynamic> createTapwaterdbRecordData({
  String? date,
  String? startTime,
  String? endTime,
  String? duration,
}) {
  final firestoreData = mapToFirestore(
    <String, dynamic>{
      'date': date,
      'start_time': startTime,
      'end_time': endTime,
      'duration': duration,
    }.withoutNulls,
  );

  return firestoreData;
}

class TapwaterdbRecordDocumentEquality implements Equality<TapwaterdbRecord> {
  const TapwaterdbRecordDocumentEquality();

  @override
  bool equals(TapwaterdbRecord? e1, TapwaterdbRecord? e2) {
    return e1?.date == e2?.date &&
        e1?.startTime == e2?.startTime &&
        e1?.endTime == e2?.endTime &&
        e1?.duration == e2?.duration;
  }

  @override
  int hash(TapwaterdbRecord? e) => const ListEquality()
      .hash([e?.date, e?.startTime, e?.endTime, e?.duration]);

  @override
  bool isValidKey(Object? o) => o is TapwaterdbRecord;
}
