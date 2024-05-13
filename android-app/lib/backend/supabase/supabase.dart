import 'package:supabase_flutter/supabase_flutter.dart' hide Provider;

export 'database/database.dart';

const _kSupabaseUrl = 'https://vrzpmdiajsdgatmsanyw.supabase.co';
const _kSupabaseAnonKey =
    'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InZyenBtZGlhanNkZ2F0bXNhbnl3Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3MTQ0ODg3ODAsImV4cCI6MjAzMDA2NDc4MH0.Q71y8hKi6we-evek7MX-wtdhR5xVYRcfUsremxDhaV4';

class SupaFlow {
  SupaFlow._();

  static SupaFlow? _instance;
  static SupaFlow get instance => _instance ??= SupaFlow._();

  final _supabase = Supabase.instance.client;
  static SupabaseClient get client => instance._supabase;

  static Future initialize() => Supabase.initialize(
        url: _kSupabaseUrl,
        anonKey: _kSupabaseAnonKey,
        debug: false,
      );
}
