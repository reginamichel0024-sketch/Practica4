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

// P4: widgets nuevos de la pestaña Temperatura (se crean en codigo, no en el .ui)
class QVBoxLayout;
class QLabel;
class QCheckBox;
class QTableWidget;
class QSpinBox;
class QLineEdit;

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

    // P4: temperatura y base SQLite
    void alPresionarLeerTemperatura();
    void cargarHistorial();
    void alPresionarGuardarNota();

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

    // P4: pestaña Temperatura
    QTimer m_leerDhtAuto;                // pide {"tipo":"leer_dht"} cada 10 s mientras la casilla este marcada
    QVBoxLayout *m_capaTemp = nullptr;   // aqui van los widgets nuevos (E1, E3-E5)
    QLabel *m_temperatura = nullptr;     // ultima lectura valida
    QCheckBox *m_guardarAuto = nullptr;  // «Guardar cada 10 s»
    QTableWidget *m_tabla = nullptr;     // id, fecha y hora, °C, nota
    QLabel *m_total = nullptr;           // SELECT COUNT(*)
    QLabel *m_aviso = nullptr;           // ultimo mensaje de la base (guardada, invalida, UPDATE...)
    QSpinBox *m_id = nullptr;            // id de la fila a anotar (E5)
    QLineEdit *m_nota = nullptr;         // texto de la nota (E5)

    void mover(const QString &accion);
    void enviarJson(const QJsonObject &objeto, bool mostrarEnRegistro = true);
    void registrar(const QString &linea);
    void agregarLectura(double distanciaCm);
    void mostrarDht(const QJsonObject &objeto);
    void habilitarControles(bool habilitar);

    // P4
    void construirPestanaTemperatura();
    bool abrirBase();
    void mostrarEsquema();
    void guardarTemperatura(double celsius);
};
#endif // MAINWINDOW_H
