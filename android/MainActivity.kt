package com.example.calinmihai

import android.Manifest
import android.os.Build
import android.os.Bundle
import android.widget.NumberPicker
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay
import java.time.LocalTime
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissionLauncher.launch(
                arrayOf(
                    Manifest.permission.BLUETOOTH_CONNECT,
                    Manifest.permission.BLUETOOTH_SCAN
                )
            )
        } else {
            permissionLauncher.launch(
                arrayOf(
                    Manifest.permission.BLUETOOTH,
                    Manifest.permission.BLUETOOTH_ADMIN
                )
            )
        }

        setContent {

            var bt by remember { mutableStateOf<BluetoothHelper?>(null) }
            var connected by remember { mutableStateOf(false) }

            var selectedTab by remember { mutableStateOf(0) }

            var cdH by remember { mutableStateOf(0) }
            var cdM by remember { mutableStateOf(0) }
            var cdS by remember { mutableStateOf(0) }

            var scheduleStartH by remember { mutableStateOf(9) }
            var scheduleStartM by remember { mutableStateOf(0) }
            var scheduleEndH by remember { mutableStateOf(11) }
            var scheduleEndM by remember { mutableStateOf(0) }

            var brightness by remember { mutableStateOf(120) }

            var lastStatus by remember { mutableStateOf("") }

            val context = LocalContext.current

            LaunchedEffect(bt) {
                while (true) {
                    lastStatus = bt?.lastStatusLine ?: ""
                    connected = bt?.isConnected == true
                    delay(200)
                }
            }

            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(
                        Brush.linearGradient(
                            listOf(Color(0xFF0D1B2A), Color(0xFF1B263B)),
                            start = Offset.Zero,
                            end = Offset.Infinite
                        )
                    )
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "Exam Clock Controller",
                            style = MaterialTheme.typography.titleLarge,
                            color = Color.White
                        )

                        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {

                            Button(
                                onClick = {
                                    val helper = BluetoothHelper("HC-05")
                                    val ok = helper.connectLegacy()
                                    if (ok) {
                                        bt = helper
                                        Toast.makeText(context, "Conectat", Toast.LENGTH_SHORT).show()
                                    } else {
                                        helper.disconnect()
                                        bt = null
                                        Toast.makeText(context, "Conectare eșuată", Toast.LENGTH_LONG).show()
                                    }
                                },
                                enabled = !connected
                            ) {
                                Text("Connect")
                            }

                            Button(
                                onClick = {
                                    bt?.sendMessage("STOP\n")
                                    bt?.disconnect()
                                    bt = null
                                },
                                enabled = connected
                            ) {
                                Text("Disconnect")
                            }
                        }
                    }

                    TabRow(
                        selectedTabIndex = selectedTab
                    ) {
                        Tab(selected = selectedTab == 0, onClick = { selectedTab = 0 }, text = { Text("Countdown") })
                        Tab(selected = selectedTab == 1, onClick = { selectedTab = 1 }, text = { Text("Ceas") })
                        Tab(selected = selectedTab == 2, onClick = { selectedTab = 2 }, text = { Text("Program") })
                        Tab(selected = selectedTab == 3, onClick = { selectedTab = 3 }, text = { Text("Luminoz.") })
                        Tab(selected = selectedTab == 4, onClick = { selectedTab = 4 }, text = { Text("Control") })
                        Tab(selected = selectedTab == 5, onClick = { selectedTab = 5 }, text = { Text("Status") })
                    }

                    when (selectedTab) {

                        0 -> {
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Column(
                                    modifier = Modifier.padding(16.dp),
                                    verticalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    Text("Setează countdown", style = MaterialTheme.typography.titleMedium)

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceEvenly
                                    ) {
                                        TimePicker("H", cdH, { cdH = it }, 0..23)
                                        TimePicker("M", cdM, { cdM = it }, 0..59)
                                        TimePicker("S", cdS, { cdS = it }, 0..59)
                                    }

                                    val total =
                                        cdH * 3600 +
                                                cdM * 60 +
                                                cdS

                                    val uiColor =
                                        when {
                                            total <= 60 -> Color.Red
                                            total <= 600 -> Color(0xFFFF9800)
                                            total <= 1800 -> Color.Yellow
                                            else -> Color.Green
                                        }

                                    Text(
                                        text = "Preview: ${formatTime(total)}",
                                        color = uiColor,
                                        style = MaterialTheme.typography.titleLarge
                                    )

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Button(
                                            onClick = {
                                                bt?.sendMessage("T=$total\n")
                                            },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("START") }

                                        Button(
                                            onClick = {
                                                bt?.sendMessage("PAUSE\n")
                                            },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("PAUSE") }

                                        Button(
                                            onClick = {
                                                bt?.sendMessage("RESUME\n")
                                            },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("RESUME") }

                                        Button(
                                            onClick = {
                                                bt?.sendMessage("STOP\n")
                                            },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("STOP") }
                                    }
                                }
                            }
                        }

                        1 -> {
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Column(
                                    modifier = Modifier.padding(16.dp),
                                    verticalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    Text("Mod ceas (sincronizare)", style = MaterialTheme.typography.titleMedium)

                                    var clockRunning by remember { mutableStateOf(false) }
                                    var now by remember { mutableStateOf(LocalTime.now()) }

                                    LaunchedEffect(clockRunning, connected) {
                                        while (clockRunning && connected) {
                                            now = LocalTime.now()
                                            bt?.sendMessage(
                                                "C=%02d:%02d:%02d\n".format(
                                                    now.hour,
                                                    now.minute,
                                                    now.second
                                                )
                                            )
                                            delay(1000)
                                        }
                                    }

                                    Text(
                                        text = "%02d:%02d:%02d".format(now.hour, now.minute, now.second),
                                        style = MaterialTheme.typography.headlineLarge
                                    )

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Button(
                                            onClick = { clockRunning = true },
                                            enabled = connected && !clockRunning,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("PORNEȘTE") }

                                        Button(
                                            onClick = { clockRunning = false },
                                            enabled = connected && clockRunning,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("OPREȘTE") }

                                        Button(
                                            onClick = {
                                                bt?.sendMessage("STOP\n")
                                                clockRunning = false
                                            },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("STOP") }
                                    }
                                }
                            }
                        }

                        2 -> {
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Column(
                                    modifier = Modifier.padding(16.dp),
                                    verticalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    Text("Program examen (START/END)", style = MaterialTheme.typography.titleMedium)

                                    Text("START", style = MaterialTheme.typography.titleSmall)
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceEvenly
                                    ) {
                                        TimePicker("H", scheduleStartH, { scheduleStartH = it }, 0..23)
                                        TimePicker("M", scheduleStartM, { scheduleStartM = it }, 0..59)
                                    }

                                    Text("END", style = MaterialTheme.typography.titleSmall)
                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.SpaceEvenly
                                    ) {
                                        TimePicker("H", scheduleEndH, { scheduleEndH = it }, 0..23)
                                        TimePicker("M", scheduleEndM, { scheduleEndM = it }, 0..59)
                                    }

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Button(
                                            onClick = {
                                                bt?.sendMessage(
                                                    "START=%02d:%02d\n".format(
                                                        scheduleStartH,
                                                        scheduleStartM
                                                    )
                                                )
                                                bt?.sendMessage(
                                                    "END=%02d:%02d\n".format(
                                                        scheduleEndH,
                                                        scheduleEndM
                                                    )
                                                )
                                            },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("TRIMITE") }

                                        Button(
                                            onClick = { bt?.sendMessage("STOP\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("STOP") }
                                    }
                                }
                            }
                        }

                        3 -> {
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Column(
                                    modifier = Modifier.padding(16.dp),
                                    verticalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    Text("Luminozitate LED", style = MaterialTheme.typography.titleMedium)

                                    Text("BRIGHT = $brightness")

                                    Slider(
                                        value = brightness.toFloat(),
                                        onValueChange = { brightness = it.roundToInt().coerceIn(0, 255) },
                                        valueRange = 0f..255f
                                    )

                                    Button(
                                        onClick = {
                                            bt?.sendMessage("BRIGHT=$brightness\n")
                                        },
                                        enabled = connected,
                                        modifier = Modifier.fillMaxWidth()
                                    ) {
                                        Text("Aplică")
                                    }
                                }
                            }
                        }

                        4 -> {
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Column(
                                    modifier = Modifier.padding(16.dp),
                                    verticalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    Text("Mesaje + control rapid", style = MaterialTheme.typography.titleMedium)

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Button(
                                            onClick = { bt?.sendMessage("MSG=EXAMEN\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("EXAMEN") }

                                        Button(
                                            onClick = { bt?.sendMessage("MSG=PAUSE\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("PAUSE") }

                                        Button(
                                            onClick = { bt?.sendMessage("MSG=STOP\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("STOP") }
                                    }

                                    Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                                    ) {
                                        Button(
                                            onClick = { bt?.sendMessage("PAUSE\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("PAUSE") }

                                        Button(
                                            onClick = { bt?.sendMessage("RESUME\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("RESUME") }

                                        Button(
                                            onClick = { bt?.sendMessage("STOP\n") },
                                            enabled = connected,
                                            modifier = Modifier.weight(1f)
                                        ) { Text("STOP") }
                                    }
                                }
                            }
                        }

                        5 -> {
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Column(
                                    modifier = Modifier.padding(16.dp),
                                    verticalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    Text("Status", style = MaterialTheme.typography.titleMedium)
                                    Text("Conectat: $connected")
                                    Text("Ultimul mesaj de la placă:")
                                    Text(
                                        text = if (lastStatus.isBlank()) "-" else lastStatus,
                                        style = MaterialTheme.typography.titleLarge
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun TimePicker(
    label: String,
    value: Int,
    onChange: (Int) -> Unit,
    range: IntRange
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label)
        AndroidView(
            factory = { context ->
                NumberPicker(context).apply {
                    minValue = range.first
                    maxValue = range.last
                    this.value = value
                    setOnValueChangedListener { _, _, newVal ->
                        onChange(newVal)
                    }
                }
            },
            modifier = Modifier
                .width(92.dp)
                .height(140.dp)
        )
    }
}

fun formatTime(seconds: Int): String {
    val h = seconds / 3600
    val m = (seconds % 3600) / 60
    val s = seconds % 60
    return "%02d:%02d:%02d".format(h, m, s)
}
