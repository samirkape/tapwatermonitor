import '../database.dart';

class StartTimeTable extends SupabaseTable<StartTimeRow> {
  @override
  String get tableName => 'start_time';

  @override
  StartTimeRow createRow(Map<String, dynamic> data) => StartTimeRow(data);
}

class StartTimeRow extends SupabaseDataRow {
  StartTimeRow(Map<String, dynamic> data) : super(data);

  @override
  SupabaseTable get table => StartTimeTable();

  DateTime get createdAt => getField<DateTime>('created_at')!;
  set createdAt(DateTime value) => setField<DateTime>('created_at', value);

  int? get startTime => getField<int>('start_time');
  set startTime(int? value) => setField<int>('start_time', value);

  String? get date => getField<String>('date');
  set date(String? value) => setField<String>('date', value);

  String get id => getField<String>('id')!;
  set id(String value) => setField<String>('id', value);

  bool get arrived => getField<bool>('arrived')!;
  set arrived(bool value) => setField<bool>('arrived', value);
}
