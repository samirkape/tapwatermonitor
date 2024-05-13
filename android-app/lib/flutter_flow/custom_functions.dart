import 'dart:convert';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:intl/intl.dart';
import 'package:timeago/timeago.dart' as timeago;
import 'lat_lng.dart';
import 'place.dart';
import 'uploaded_file.dart';
import '/backend/backend.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import '/backend/supabase/supabase.dart';

List<TapwaterdbRow> sortByDateFunc(
  List<TapwaterdbRow> tapwaters,
  int index,
  bool order,
) {
  switch (index) {
    case 0:
      tapwaters.sort((a, b) => (a.date ?? '').compareTo(b.date ?? ''));
      break;
    case 1:
      tapwaters
          .sort((a, b) => (a.startTime ?? '').compareTo(b.startTime ?? ''));
      break;
    case 2:
      tapwaters.sort((a, b) => (a.endTime ?? '').compareTo(b.endTime ?? ''));
      break;
    case 3:
      tapwaters.sort((a, b) => (a.duration ?? 0).compareTo(b.duration as num));
      break;
    default:
      break;
  }
  if (!order) {
    tapwaters = tapwaters.reversed.toList();
  }
  return tapwaters;
}
