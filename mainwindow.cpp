#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QJsonDocument>
#include <QTime>
#include <QShortcut>
// P4: pestañas, widgets nuevos y base SQLite
#include <QJsonValue>
#include <QDateTime>
#include <QGraphicsLayout>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

// ----------------------------------------------------------------------
// Acciones que manda cada boton. En la practica 1 el boton "Adelante"
// mandaba "retroceder" y "Atras" mandaba "avanzar" (los motores del
// carrito estan cableados al reves). Se respeta ese comportamiento.
// Si alguna direccion queda invertida, cambiala SOLO aqui.
// ----------------------------------------------------------------------
static const char *ACCION_ADELANTE  = "retroceder";
static const char *ACCION_ATRAS     = "avanzar";
static const char *ACCION_IZQUIERDA = "izquierda";
static const char *ACCION_DERECHA   = "derecha";
static const int   DURACION_MS      = 100;   // cada pulso de movimiento dura 100 ms

// ----------------------------------------------------------------------
// P4 · Regla antiplagio: ALUMNO = «Nombre Apellido · codigo · temperatura_XXXX.db»
// Solo cambia CODIGO: el nombre de la base (ultimos 4 digitos) y la etiqueta se arman solos.
// ----------------------------------------------------------------------
static const char *NOMBRE = "Michel Madero Regina";
static const char *CODIGO = "221800244";   // <-- CAMBIA: tu codigo completo
static QString nombreBase() { return "temperatura_" + QString::fromLatin1(CODIGO).right(4) + ".db"; }
static QString alumno()
{
    return QString::fromUtf8(NOMBRE) + " · " + QString::fromLatin1(CODIGO) + " · " + nombreBase();
}

// Debe coincidir con el firmware: "dht22" si SIMULAR_DHT es false, "simulada" si es true.
static const char *FUENTE = "dht22";
static const int MS_LEER_DHT = 10000;   // «Guardar cada 10 s» (el DHT22 no se lee mas rapido que cada 2 s)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ---- P4: la interfaz de la P3 (la del .ui) pasa a la pestaña «Robot» ----
    // El .ui se acomodo en vertical y compacto para que quepa en el telefono sin deslizar.
    QWidget *paginaRobot = takeCentralWidget();

    auto *central = new QWidget;
    auto *raiz = new QVBoxLayout(central);
    raiz->setContentsMargins(4, 4, 4, 4);
    auto *etiquetaAlumno = new QLabel(alumno());     // visible en las dos pestañas
    etiquetaAlumno->setAlignment(Qt::AlignCenter);
    etiquetaAlumno->setWordWrap(true);
    etiquetaAlumno->setStyleSheet("font-weight: bold; color: #1F4E79;");
    raiz->addWidget(etiquetaAlumno);

    auto *pestanas = new QTabWidget;
    pestanas->addTab(paginaRobot, tr("Robot"));      // lo de la P3 (sin el contador de la P2)
    auto *pagTemp = new QWidget;
    m_capaTemp = new QVBoxLayout(pagTemp);           // aqui van los widgets nuevos (E1, E3-E5)
    pestanas->addTab(pagTemp, tr("Temperatura"));
    raiz->addWidget(pestanas, 1);
    setCentralWidget(central);
    setWindowTitle(tr("Práctica 4 - Carrito ESP32-S3 - Michel"));
    construirPestanaTemperatura();

    ui->registro->setReadOnly(true);
    habilitarControles(false);             // sin conexion no se puede mover nada

    // ---- Teclado: flechas para mover el carrito ----
    // QShortcut funciona aunque el foco este en otro widget (el slider, un boton...).
    // Mientras escribes en la caja de IP, las flechas siguen moviendo el cursor del texto.
    auto atajo = [this](Qt::Key tecla, QPushButton *boton) {
        auto *s = new QShortcut(QKeySequence(tecla), this);
        connect(s, &QShortcut::activated, boton, &QPushButton::click);
    };
    atajo(Qt::Key_Up,    ui->botonAdelante);
    atajo(Qt::Key_Down,  ui->botonAtras);
    atajo(Qt::Key_Left,  ui->botonIzquierda);
    atajo(Qt::Key_Right, ui->botonDerecha);

    // ---- Señales del WebSocket: connect + lambdas ----
    connect(&socket, &QWebSocket::connected, this, [this]() {
        serie->clear();                    // la grafica empieza de cero en cada conexion
        ejeTiempo->setRange(0, 10);
        reloj.start();
        ui->etiquetaEstado->setText("Conectado");
        ui->botonConectar->setText("Desconectar");
        habilitarControles(true);
        registrar("Conectado a la ESP32.");
        temporizadorLatido.start();
        temporizadorDistancia.start();
    });

    connect(&socket, &QWebSocket::disconnected, this, [this]() {
        temporizadorLatido.stop();
        temporizadorDistancia.stop();
        m_leerDhtAuto.stop();              // P4: sin conexion no se piden lecturas
        m_guardarAuto->setChecked(false);
        ui->etiquetaEstado->setText("Desconectado");
        ui->botonConectar->setText("Conectar");
        habilitarControles(false);
        registrar("Desconectado.");
    });

    connect(&socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        registrar("Error: " + socket.errorString());
    });

    connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString &mensaje) {
        QJsonObject objeto = QJsonDocument::fromJson(mensaje.toUtf8()).object();
        QString tipo = objeto["tipo"].toString();

        // Mensajes muy frecuentes: se muestran en la UI pero no llenan el registro
        if (tipo == "distancia_lectura") {
            agregarLectura(objeto["d"].toDouble());
            return;
        }
        if (tipo == "telemetria") {
            mostrarDht(objeto);
            return;
        }
        if (tipo == "latido_ok") {
            return;
        }
        if (tipo == "mover_ack" || tipo == "mover_inicio" || tipo == "mover_fin") {
            ui->statusbar->showMessage(mensaje, 1000);
            return;
        }

        // P4 · E1: respuesta a {"tipo":"leer_dht"}. Se decide con dht_ok, no con el numero:
        // si el sensor falla, el firmware repite la ultima lectura buena o manda null.
        if (tipo == "dht_lectura") {
            registrar("RX " + mensaje);                  // deja ver el JSON recibido
            mostrarDht(objeto);                          // tambien actualiza el recuadro Sensores
            const QJsonValue t = objeto.value("temperatura");
            if (!objeto.value("dht_ok").toBool() || !t.isDouble()) {
                registrar("Lectura inválida: no se guarda");
                m_aviso->setText("Lectura inválida: no se guarda");
                return;
            }
            m_temperatura->setText(QString::number(t.toDouble(), 'f', 1) + " °C");
            guardarTemperatura(t.toDouble());
            return;
        }

        registrar("<- " + mensaje);
        if ((tipo == "saludo" || tipo == "servo_ack") && objeto.contains("angulo")) {
            int angulo = objeto["angulo"].toInt();
            ui->etiquetaConfirmado->setText(QString::number(angulo) + "°");
        }
    });

    // ---- Latido cada 1 s (igual que en la practica 1) ----
    temporizadorLatido.setInterval(1000);
    connect(&temporizadorLatido, &QTimer::timeout, this, [this]() {
        QJsonObject comando;
        comando["tipo"] = "latido";
        enviarJson(comando, false);
    });

    // ---- Lectura del HC-SR04: 10 veces por segundo ----
    temporizadorDistancia.setInterval(100);
    connect(&temporizadorDistancia, &QTimer::timeout, this, [this]() {
        QJsonObject comando;
        comando["tipo"] = "leer_distancia";
        enviarJson(comando, false);
    });

    // ---- Grafica del HC-SR04 (Qt Charts) ----
    grafica->addSeries(serie);
    grafica->legend()->hide();
    // P4: grafica compacta para el telefono: sin titulos grandes y margenes minimos
    grafica->setTitle("HC-SR04: distancia (cm) vs tiempo (s)");
    QFont letraChica = grafica->titleFont();
    letraChica.setPointSize(8);
    grafica->setTitleFont(letraChica);
    grafica->setMargins(QMargins(0, 0, 0, 0));
    grafica->layout()->setContentsMargins(0, 0, 0, 0);
    ejeTiempo->setRange(0, 10);
    ejeTiempo->setLabelsFont(letraChica);
    ejeDistancia->setRange(0, 100);          // P4: escala de 0 a 100 cm
    ejeDistancia->setTickCount(5);           // marcas en 0, 25, 50, 75 y 100 cm
    ejeDistancia->setLabelFormat("%d");
    ejeDistancia->setLabelsFont(letraChica);
    grafica->addAxis(ejeTiempo, Qt::AlignBottom);
    grafica->addAxis(ejeDistancia, Qt::AlignLeft);
    serie->attachAxis(ejeTiempo);
    serie->attachAxis(ejeDistancia);
    ui->graficaDistancia->setChart(grafica);
    ui->graficaDistancia->setRenderHint(QPainter::Antialiasing);

    // ---- Limite de envio del servo: el temporizador manda el angulo pendiente ----
    temporizadorServo.setInterval(50);
    connect(&temporizadorServo, &QTimer::timeout, this, [this]() {
        if (anguloPendiente < 0) {          // no hay nada nuevo: se detiene
            temporizadorServo.stop();
            return;
        }
        QJsonObject comando;
        comando["tipo"] = "servo";
        comando["angulo"] = anguloPendiente;
        enviarJson(comando);
        anguloPendiente = -1;
    });

    // ---- P4 · E3: lectura automatica de temperatura ----
    m_leerDhtAuto.setInterval(MS_LEER_DHT);
    connect(&m_leerDhtAuto, &QTimer::timeout, this, &MainWindow::alPresionarLeerTemperatura);

    // ---- P4 · E2: abre (o crea) la base; si se abrio, muestra el esquema y el historial ----
    if (abrirBase()) {
        mostrarEsquema();

    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ======================================================================
//                              CONEXION
// ======================================================================
void MainWindow::on_botonConectar_clicked()
{
    if (socket.state() == QAbstractSocket::ConnectedState) {
        socket.close();
        return;
    }
    QString url = "ws://" + ui->lineEditIp->text().trimmed() + ":" + QString::number(ui->spinPuerto->value()) + "/";
    registrar("Conectando a " + url + " ...");
    ui->etiquetaEstado->setText("Conectando...");
    socket.open(QUrl(url));
}

void MainWindow::habilitarControles(bool habilitar)
{
    ui->grupoMovimiento->setEnabled(habilitar);
    ui->grupoServo->setEnabled(habilitar);
    ui->grupoSensores->setEnabled(habilitar);
}

// ======================================================================
//                         MOVIMIENTO (practica 1)
// ======================================================================
void MainWindow::mover(const QString &accion)
{
    QJsonObject comando;
    comando["tipo"] = "mover";
    comando["accion"] = accion;
    comando["ms"] = DURACION_MS;
    enviarJson(comando, false);             // no se registra: con la flecha presionada son muchos
}

void MainWindow::on_botonAdelante_clicked()  { mover(ACCION_ADELANTE); }
void MainWindow::on_botonAtras_clicked()     { mover(ACCION_ATRAS); }
void MainWindow::on_botonIzquierda_clicked() { mover(ACCION_IZQUIERDA); }
void MainWindow::on_botonDerecha_clicked()   { mover(ACCION_DERECHA); }

// ======================================================================
//                         SERVO (trabajo de clase)
// ======================================================================
void MainWindow::on_deslizadorAngulo_valueChanged(int value)
{
    ui->etiquetaPedido->setText(QString::number(value) + "°");
    anguloPendiente = value;
    if (!temporizadorServo.isActive()) {
        temporizadorServo.start();          // envia en el siguiente tick (<= 50 ms)
    }
}

// ======================================================================
//                                DHT22
// ======================================================================
// No hay boton: la ESP32 manda "telemetria" cada 2 s (con temperatura y
// humedad) mientras reciba el latido, y aqui se actualizan las etiquetas.
void MainWindow::mostrarDht(const QJsonObject &objeto)
{
    if (!objeto["dht_ok"].toBool()) {
        ui->etiquetaTemperatura->setText("Error");
        ui->etiquetaHumedad->setText("Error");
        return;
    }
    double t = objeto["temperatura"].toDouble();
    double h = objeto["humedad"].toDouble();
    ui->etiquetaTemperatura->setText(QString::number(t, 'f', 1) + " °C");
    ui->etiquetaHumedad->setText(QString::number(h, 'f', 1) + " %");
}

// ======================================================================
//                               HC-SR04
// ======================================================================
void MainWindow::agregarLectura(double distanciaCm)
{
    // 0 cm = el sensor no recibio eco (nada al frente o fuera de rango)
    if (distanciaCm <= 0 || distanciaCm > 400) {
        ui->etiquetaDistancia->setText("Fuera de rango");
        return;
    }
    ui->etiquetaDistancia->setText(QString::number(distanciaCm, 'f', 1) + " cm");

    double t = reloj.elapsed() / 1000.0;           // segundos desde que se conecto
    serie->append(t, qMin(distanciaCm, 100.0));    // P4: arriba de 100 cm se dibuja en el tope
    if (serie->count() > 200) {                    // guarda solo los ultimos 20 s
        serie->remove(0);
    }
    if (t > 10) {
        ejeTiempo->setRange(t - 10, t);            // ventana de 10 s que avanza
    }
}

// ======================================================================
//                              AUXILIARES
// ======================================================================
void MainWindow::enviarJson(const QJsonObject &objeto, bool mostrarEnRegistro)
{
    if (socket.state() != QAbstractSocket::ConnectedState) return;
    QString texto = QJsonDocument(objeto).toJson(QJsonDocument::Compact);
    socket.sendTextMessage(texto);
    if (mostrarEnRegistro) registrar("-> " + texto);
}

void MainWindow::registrar(const QString &linea)
{
    QString hora = QTime::currentTime().toString("HH:mm:ss.zzz");
    ui->registro->appendPlainText(hora + "  " + linea);
}

// ======================================================================
//                   P4 · PESTAÑA TEMPERATURA (en codigo)
// ======================================================================
void MainWindow::construirPestanaTemperatura()
{
    m_capaTemp->setSpacing(8);

    // E1 y E3: ultima lectura, boton y modo automatico
    auto *cajaLectura = new QGroupBox(tr("Temperatura del robot (DHT22)"));
    auto *colLectura = new QVBoxLayout(cajaLectura);
    m_temperatura = new QLabel(QStringLiteral("— °C"));
    m_temperatura->setAlignment(Qt::AlignCenter);
    QFont grande = m_temperatura->font();
    grande.setPointSize(grande.pointSize() + 6);
    grande.setBold(true);
    m_temperatura->setFont(grande);
    colLectura->addWidget(m_temperatura);
    auto *filaLectura = new QHBoxLayout;
    auto *leer = new QPushButton(tr("Leer temperatura"));
    m_guardarAuto = new QCheckBox(tr("Guardar cada 10 s"));
    filaLectura->addWidget(leer, 1);
    filaLectura->addWidget(m_guardarAuto);
    colLectura->addLayout(filaLectura);
    connect(leer, &QPushButton::clicked, this, &MainWindow::alPresionarLeerTemperatura);
    connect(m_guardarAuto, &QCheckBox::toggled, this, [this](bool activo) {
        if (activo) {
            alPresionarLeerTemperatura();   // primera lectura de inmediato
            m_leerDhtAuto.start();
        } else {
            m_leerDhtAuto.stop();
        }
    });
    m_capaTemp->addWidget(cajaLectura);

    // E4: historial
    auto *cajaHist = new QGroupBox(tr("Historial (base %1)").arg(nombreBase()));
    auto *colHist = new QVBoxLayout(cajaHist);
    auto *filaHist = new QHBoxLayout;
    auto *cargar = new QPushButton(tr("Cargar historial"));
    m_total = new QLabel(tr("Mediciones guardadas: —"));
    filaHist->addWidget(cargar);
    filaHist->addWidget(m_total, 1);
    colHist->addLayout(filaHist);
    connect(cargar, &QPushButton::clicked, this, &MainWindow::cargarHistorial);

    m_tabla = new QTableWidget(0, 4);
    m_tabla->setHorizontalHeaderLabels({tr("id"), tr("fecha y hora"), tr("°C"), tr("nota")});
    m_tabla->setEditTriggers(QAbstractItemView::NoEditTriggers);   // solo lectura (ep. 03)
    m_tabla->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tabla->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tabla->verticalHeader()->setVisible(false);    // el numero de fila NO es el id
    m_tabla->horizontalHeader()->setStretchLastSection(true);
    m_tabla->setMinimumHeight(180);
    colHist->addWidget(m_tabla, 1);
    m_capaTemp->addWidget(cajaHist, 1);

    // E5: nota con UPDATE ... WHERE id
    auto *cajaNota = new QGroupBox(tr("Anotar una medición"));
    auto *colNota = new QVBoxLayout(cajaNota);
    auto *filaId = new QHBoxLayout;
    m_id = new QSpinBox;
    m_id->setRange(0, 99999);
    m_id->setSpecialValueText(QStringLiteral("—"));   // 0 = ninguna fila elegida
    m_id->setPrefix(tr("id "));
    m_nota = new QLineEdit;
    m_nota->setPlaceholderText(tr("Nota, p. ej. ventana abierta"));
    filaId->addWidget(m_id);
    filaId->addWidget(m_nota, 1);
    colNota->addLayout(filaId);
    auto *guardarNota = new QPushButton(tr("Guardar nota"));
    colNota->addWidget(guardarNota);
    connect(guardarNota, &QPushButton::clicked, this, &MainWindow::alPresionarGuardarNota);
    connect(m_tabla, &QTableWidget::cellClicked, this, [this](int fila, int) {
        m_id->setValue(m_tabla->item(fila, 0)->text().toInt());   // columna 0 = id
    });
    m_capaTemp->addWidget(cajaNota);


    // Aviso: el ultimo mensaje de la base tambien se ve en esta pestaña
    m_aviso = new QLabel;
    m_aviso->setWordWrap(true);
    m_aviso->setStyleSheet("color: #555;");
    m_capaTemp->addWidget(m_aviso);
}

void MainWindow::alPresionarLeerTemperatura()
{
    if (socket.state() != QAbstractSocket::ConnectedState) {
        m_aviso->setText(tr("Conéctate al robot primero"));
        return;
    }
    QJsonObject comando;
    comando["tipo"] = "leer_dht";
    enviarJson(comando);
}

// ======================================================================
//                         P4 · BASE SQLITE
// ======================================================================
// E2: abre (o crea) temperatura_XXXX.db en la memoria privada de la app
bool MainWindow::abrirBase()
{
    const QString carpeta = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(carpeta);
    const QString ruta = carpeta + "/" + nombreBase();
    const bool yaExistia = QFile::exists(ruta);

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(ruta);
    if (!db.open()) {
        registrar("No se abrió la base: " + db.lastError().text());
        return false;
    }
    QSqlQuery consulta;
    if (!consulta.exec("CREATE TABLE IF NOT EXISTS mediciones ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "   // lo asigna SQLite
                       "fecha_hora TEXT NOT NULL, "    // cuando llego la lectura (ISO 8601)
                       "temperatura REAL NOT NULL, "   // °C con un decimal; nunca un 0 inventado
                       "fuente TEXT NOT NULL, "        // dht22 o simulada
                       "nota TEXT)")) {                // vacia; se llena con UPDATE en E5
        registrar("No se creó la tabla: " + consulta.lastError().text());
        return false;
    }
    registrar((yaExistia ? "Base encontrada: " : "Base nueva creada: ") + ruta);
    m_aviso->setText(yaExistia ? tr("Base encontrada") : tr("Base nueva creada"));
    return true;
}

// E2d: esquema de la tabla (deben salir 5 columnas)
void MainWindow::mostrarEsquema()
{
    QSqlQuery consulta("PRAGMA table_info(mediciones)");
    while (consulta.next()) {
        // columnas de PRAGMA table_info: cid, name, type, notnull, dflt_value, pk
        QString linea = QString("columna %1: %2 %3")
                            .arg(consulta.value(0).toInt())
                            .arg(consulta.value(1).toString())
                            .arg(consulta.value(2).toString());
        if (consulta.value(3).toInt()) linea += " NOT NULL";
        if (consulta.value(5).toInt()) linea += " PK";
        registrar(linea);
    }
}

// E3: INSERT con prepare + bindValue
void MainWindow::guardarTemperatura(double celsius)
{
    const double c1 = qRound(celsius * 10) / 10.0;   // un decimal: 24.60000038 -> 24.6
    QSqlQuery consulta;
    consulta.prepare("INSERT INTO mediciones (fecha_hora, temperatura, fuente) "
                     "VALUES (:fecha_hora, :temperatura, :fuente)");
    consulta.bindValue(":fecha_hora", QDateTime::currentDateTime().toString(Qt::ISODate));
    consulta.bindValue(":temperatura", c1);
    consulta.bindValue(":fuente", QString::fromUtf8(FUENTE));
    if (!consulta.exec()) {
        registrar("INSERT falló: " + consulta.lastError().text());
        return;
    }
    const QString mensaje = QString("Guardada %1 °C con id %2").arg(c1, 0, 'f', 1)
                                .arg(consulta.lastInsertId().toString());
    registrar(mensaje);
    m_aviso->setText(mensaje);
    cargarHistorial();   // E4: recarga despues de cada INSERT
}

// E4: SELECT de las ultimas 50 y total con COUNT(*)
void MainWindow::cargarHistorial()
{
    QSqlQuery consulta("SELECT id, fecha_hora, temperatura, nota FROM mediciones "
                       "ORDER BY id DESC LIMIT 50");
    if (!consulta.isActive()) {
        registrar("SELECT falló: " + consulta.lastError().text());
        return;
    }
    m_tabla->setRowCount(0);
    while (consulta.next()) {
        const int fila = m_tabla->rowCount();
        m_tabla->insertRow(fila);
        m_tabla->setItem(fila, 0, new QTableWidgetItem(consulta.value(0).toString()));
        m_tabla->setItem(fila, 1, new QTableWidgetItem(consulta.value(1).toString()));
        m_tabla->setItem(fila, 2, new QTableWidgetItem(
                             QString::number(consulta.value(2).toDouble(), 'f', 1)));
        m_tabla->setItem(fila, 3, new QTableWidgetItem(consulta.value(3).toString()));
    }
    m_tabla->resizeColumnsToContents();
    QSqlQuery total("SELECT COUNT(*) FROM mediciones");
    if (total.next())
        m_total->setText(tr("Mediciones guardadas: ") + total.value(0).toString());
}

// E5: UPDATE ... WHERE id
void MainWindow::alPresionarGuardarNota()
{
    const int id = m_id->value();
    if (id == 0) {
        m_aviso->setText(tr("Primero toca una fila"));
        registrar("Primero toca una fila");
        return;
    }
    QSqlQuery consulta;
    consulta.prepare("UPDATE mediciones SET nota = :nota WHERE id = :id");
    consulta.bindValue(":nota", m_nota->text());
    consulta.bindValue(":id", id);
    if (!consulta.exec()) {
        registrar("UPDATE falló: " + consulta.lastError().text());
        return;
    }
    const QString mensaje = QString("UPDATE id %1 «%2»: %3 fila(s) cambiada(s)")
                                .arg(id).arg(m_nota->text()).arg(consulta.numRowsAffected());
    registrar(mensaje);
    m_aviso->setText(mensaje);
    cargarHistorial();
}

