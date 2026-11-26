#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QJsonDocument>
#include <QTime>
#include <QShortcut>

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

// Contador de objetos: el objeto "se va" hasta que la distancia supera el
// umbral + HISTERESIS. Asi el ruido del sensor cerca del umbral no cuenta doble.
static const double HISTERESIS_CM   = 5.0;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

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

        registrar("<- " + mensaje);
        if (tipo == "dht_lectura") {
            mostrarDht(objeto);
        }
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
    grafica->setTitle("Distancia del sensor HC-SR04");
    ejeTiempo->setTitleText("Tiempo (s)");
    ejeTiempo->setRange(0, 10);
    ejeDistancia->setTitleText("Distancia (cm)");
    ejeDistancia->setRange(0, 200);          // el sensor mide de 2 a 400 cm; 200 se ve mejor
    ejeDistancia->setTickCount(5);           // marcas en 0, 50, 100, 150 y 200 cm
    grafica->addAxis(ejeTiempo, Qt::AlignBottom);
    grafica->addAxis(ejeDistancia, Qt::AlignLeft);
    serie->attachAxis(ejeTiempo);
    serie->attachAxis(ejeDistancia);

    // Linea horizontal roja punteada que marca el umbral del contador
    QPen plumaUmbral(Qt::red);
    plumaUmbral.setStyle(Qt::DashLine);
    plumaUmbral.setWidth(2);
    lineaUmbral->setPen(plumaUmbral);
    grafica->addSeries(lineaUmbral);
    lineaUmbral->attachAxis(ejeTiempo);
    lineaUmbral->attachAxis(ejeDistancia);
    dibujarUmbral();
    // Se redibuja cuando la ventana de tiempo avanza o cuando cambias el umbral
    connect(ejeTiempo, &QValueAxis::rangeChanged, this, [this]() { dibujarUmbral(); });
    connect(ui->spinUmbral, &QSpinBox::valueChanged, this, [this]() { dibujarUmbral(); });
    mostrarContador();
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
    ui->grupoContador->setEnabled(habilitar);
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
        actualizarContador(1000);                  // sin eco = no hay nada enfrente
        return;
    }
    actualizarContador(distanciaCm);
    ui->etiquetaDistancia->setText(QString::number(distanciaCm, 'f', 1) + " cm");

    double t = reloj.elapsed() / 1000.0;           // segundos desde que se conecto
    serie->append(t, distanciaCm);
    if (serie->count() > 200) {                    // guarda solo los ultimos 20 s
        serie->remove(0);
    }
    if (t > 10) {
        ejeTiempo->setRange(t - 10, t);            // ventana de 10 s que avanza
    }
}

// ======================================================================
//                    CONTADOR DE OBJETOS (extra HC-SR04)
// ======================================================================
// Se cuenta un objeto cuando la distancia BAJA del umbral (llega algo).
// No se vuelve a contar hasta que la distancia SUBE de umbral + histeresis
// (el objeto ya se fue). Asi un objeto que se queda enfrente cuenta 1 vez.
void MainWindow::actualizarContador(double distanciaCm)
{
    double umbral = ui->spinUmbral->value();

    if (!objetoPresente && distanciaCm < umbral) {
        objetoPresente = true;
        contadorObjetos++;
        registrar("Objeto #" + QString::number(contadorObjetos) +
                  " detectado a " + QString::number(distanciaCm, 'f', 1) + " cm");
        mostrarContador();
    } else if (objetoPresente && distanciaCm > umbral + HISTERESIS_CM) {
        objetoPresente = false;
        mostrarContador();
    }
}

void MainWindow::mostrarContador()
{
    ui->etiquetaContador->setText(QString::number(contadorObjetos));
    if (objetoPresente) {
        ui->etiquetaPresencia->setText("Objeto enfrente");
        ui->etiquetaPresencia->setStyleSheet("color: white; background-color: #c0392b; padding: 4px;");
    } else {
        ui->etiquetaPresencia->setText("Libre");
        ui->etiquetaPresencia->setStyleSheet("color: white; background-color: #27ae60; padding: 4px;");
    }
}

void MainWindow::on_botonReiniciarContador_clicked()
{
    contadorObjetos = 0;
    objetoPresente = false;
    mostrarContador();
    registrar("Contador reiniciado.");
}

void MainWindow::dibujarUmbral()
{
    double umbral = ui->spinUmbral->value();
    lineaUmbral->clear();
    lineaUmbral->append(ejeTiempo->min(), umbral);
    lineaUmbral->append(ejeTiempo->max(), umbral);
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
