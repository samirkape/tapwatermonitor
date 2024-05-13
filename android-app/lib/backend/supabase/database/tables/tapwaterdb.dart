import '../database.dart';

class TapwaterdbTable extends SupabaseTable<TapwaterdbRow> {
  @override
  String get tableName => 'tapwaterdb';

  @override
  TapwaterdbRow createRow(Map<String, dynamic> data) => TapwaterdbRow(data);
}

class TapwaterdbRow extends SupabaseDataRow {
  TapwaterdbRow(Map<String, dynamic> data) : super(data);

  @override
  SupabaseTable get table => TapwaterdbTable();

  int get id => getField<int>('id')!;
  set id(int value) => setField<int>('id', value);

  DateTime get createdAt => getField<DateTime>('created_at')!;
  set createdAt(DateTime value) => setField<DateTime>('created_at', value);

  String? get date => getField<String>('date');
  set date(String? value) => setField<String>('date', value);

  String? get startTime => getField<String>('start_time');
  set startTime(String? value) => setField<String>('start_time', value);

  String? get endTime => getField<String>('end_time');
  set endTime(String? value) => setField<String>('end_time', value);

  int? get duration => getField<int>('duration');
  set duration(int? value) => setField<int>('duration', value);
}
