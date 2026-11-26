#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>
#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_botonConectar_clicked();

    // Movimiento (practica 1)
    void on_botonAdelante_clicked();
    void on_botonAtras_clicked();
    void on_botonIzquierda_clicked();
    void on_botonDerecha_clicked();

    // Servo (trabajo de clase)
    void on_deslizadorAngulo_valueChanged(int value);

    // Contador de objetos (extra con el HC-SR04)
    void on_botonReiniciarContador_clicked();

private:
    Ui::MainWindow *ui;

    QWebSocket socket;          // cliente WebSocket (objeto, no puntero)

    QTimer temporizadorServo;   // limite de envio del servo: 1 mensaje cada 50 ms
    int anguloPendiente = -1;   // ultimo angulo pedido que falta enviar

    QTimer temporizadorLatido;    // latido cada 1 s: activa la telemetria (DHT22 cada 2 s)
    QTimer temporizadorDistancia; // pide una lectura del HC-SR04 cada 100 ms

    // Grafica del HC-SR04. QChart se queda con la serie y los ejes,
    // y la vista se queda con la grafica: por eso estos cuatro son punteros.
    QChart *grafica = new QChart;
    QLineSeries *serie = new QLineSeries;
    QValueAxis *ejeTiempo = new QValueAxis;
    QValueAxis *ejeDistancia = new QValueAxis;
    QElapsedTimer reloj;        // segundos desde que se conecto

    // Contador de objetos: cuenta cada vez que algo pasa frente al sensor
    int  contadorObjetos = 0;       // cuantos objetos han pasado
    bool objetoPresente  = false;   // true mientras el objeto sigue enfrente
    QLineSeries *lineaUmbral = new QLineSeries;   // linea roja del umbral en la grafica

    void mover(const QString &accion);
    void enviarJson(const QJsonObject &objeto, bool mostrarEnRegistro = true);
    void registrar(const QString &linea);
    void agregarLectura(double distanciaCm);
    void mostrarDht(const QJsonObject &objeto);
    void habilitarControles(bool habilitar);
    void actualizarContador(double distanciaCm);
    void mostrarContador();
    void dibujarUmbral();
};
#endif // MAINWINDOW_H
