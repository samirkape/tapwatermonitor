import 'package:firebase_core/firebase_core.dart';
import 'package:flutter/foundation.dart';

Future initFirebase() async {
  if (kIsWeb) {
    await Firebase.initializeApp(
        options: FirebaseOptions(
            apiKey: "AIzaSyABOvOWiVx8vaU1VxJOM_xlmjfMMlQpzPY",
            authDomain: "tapwater-1cd03.firebaseapp.com",
            projectId: "tapwater-1cd03",
            storageBucket: "tapwater-1cd03.appspot.com",
            messagingSenderId: "1061699000371",
            appId: "1:1061699000371:web:e7486a97db478130ee2b46",
            measurementId: "G-P6D653BT4K"));
  } else {
    await Firebase.initializeApp();
  }
}
