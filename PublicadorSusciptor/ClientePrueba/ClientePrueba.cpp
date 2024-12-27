
#include <winsock2.h>
#include <enet/enet.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <mutex>
#include <string> // Asegúrate de incluir este encabezado
#include <sstream> // Alternativa para std::to_string si es necesario
struct TestMetrics {
    int mensajesEnviados = 0;
    int mensajesRecibidos = 0;
    int conexionesFallidas = 0;
    double latenciaPromedio = 0;
    std::chrono::milliseconds tiempoTotal{ 0 };
};

class ClientePruebaCarga {
private:
    ENetHost* client;
    ENetPeer* peer;
    bool conectado;
    TestMetrics metrics;
    std::mutex metricsMutex;
    int clientId;

public:
    ClientePruebaCarga(int id) : client(nullptr), peer(nullptr), conectado(false), clientId(id) {
        if (enet_initialize() != 0) {
            std::cout << "Cliente " << id << ": Error al inicializar ENet" << std::endl;
            return;
        }

        client = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!client) {
            std::cout << "Cliente " << id << ": Error al crear el cliente" << std::endl;
            return;
        }
    }

    ~ClientePruebaCarga() {
        desconectar();
        if (client) {
            enet_host_destroy(client);
        }
        enet_deinitialize();
    }

    bool conectar() {
        ENetAddress direccion;
        enet_address_set_host(&direccion, "127.0.0.1");
        direccion.port = 1234;

        peer = enet_host_connect(client, &direccion, 2, 0);
        if (!peer) {
            std::lock_guard<std::mutex> lock(metricsMutex);
            metrics.conexionesFallidas++;
            return false;
        }

        ENetEvent evento;
        if (enet_host_service(client, &evento, 5000) > 0 &&
            evento.type == ENET_EVENT_TYPE_CONNECT) {
            conectado = true;
            return true;
        }

        std::lock_guard<std::mutex> lock(metricsMutex);
        metrics.conexionesFallidas++;
        enet_peer_reset(peer);
        return false;
    }

    void desconectar() {
        if (peer && conectado) {
            enet_peer_disconnect(peer, 0);
            ENetEvent evento;
            while (enet_host_service(client, &evento, 3000) > 0) {
                if (evento.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(evento.packet);
                }
            }
            enet_peer_reset(peer);
            conectado = false;
        }
    }

    void publicar(const std::string& tema, const std::string& mensaje) {
        if (!conectado) return;

        auto inicio = std::chrono::high_resolution_clock::now();

        std::string mensajeCompleto = "PUB:" + tema + ":" + mensaje;
        ENetPacket* paquete = enet_packet_create(
            mensajeCompleto.c_str(),
            mensajeCompleto.length(),
            ENET_PACKET_FLAG_RELIABLE
        );

        if (enet_peer_send(peer, 0, paquete) == 0) {
            std::lock_guard<std::mutex> lock(metricsMutex);
            metrics.mensajesEnviados++;

            auto fin = std::chrono::high_resolution_clock::now();
            auto duracion = std::chrono::duration_cast<std::chrono::milliseconds>(fin - inicio);
            metrics.latenciaPromedio = (metrics.latenciaPromedio * (metrics.mensajesEnviados - 1) +
                duracion.count()) / metrics.mensajesEnviados;
        }
    }

    void suscribirse(const std::string& tema) {
        if (!conectado) return;

        std::string mensaje = "SUB:" + tema + ":";
        ENetPacket* paquete = enet_packet_create(
            mensaje.c_str(),
            mensaje.length(),
            ENET_PACKET_FLAG_RELIABLE
        );

        enet_peer_send(peer, 0, paquete);
    }

    void actualizarEventos() {
        if (!conectado) return;

        ENetEvent evento;
        while (enet_host_service(client, &evento, 0) > 0) {
            switch (evento.type) {
            case ENET_EVENT_TYPE_RECEIVE: {
                std::lock_guard<std::mutex> lock(metricsMutex);
                metrics.mensajesRecibidos++;
                enet_packet_destroy(evento.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
                conectado = false;
                return;
            }
        }
    }

    const TestMetrics& obtenerMetricas() const {
        return metrics;
    }
};

void ejecutarPruebaDeRendimiento(int numClientes, int numMensajes, int duracionSegundos) {
    std::cout << "\nIniciando prueba de rendimiento con " << numClientes
        << " clientes, " << numMensajes << " mensajes por cliente\n";

    std::vector<std::unique_ptr<ClientePruebaCarga>> publicadores;
    std::vector<std::unique_ptr<ClientePruebaCarga>> suscriptores;

    // Crear y conectar clientes
    for (int i = 0; i < numClientes; i++) {
        auto pub = std::make_unique<ClientePruebaCarga>(i);
        auto sub = std::make_unique<ClientePruebaCarga>(i + numClientes);

        if (pub->conectar() && sub->conectar()) {
            sub->suscribirse("prueba" + std::to_string(i));
            publicadores.push_back(std::move(pub));
            suscriptores.push_back(std::move(sub));
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Ejecutar la prueba
    auto inicio = std::chrono::high_resolution_clock::now();
    bool ejecutando = true;
    int mensajesEnviados = 0;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100, 1000);

    while (ejecutando) {
        for (size_t i = 0; i < publicadores.size() && mensajesEnviados < numMensajes; i++) {
            publicadores[i]->publicar(
                "prueba" + std::to_string(i),
                "Mensaje de prueba " + std::to_string(mensajesEnviados)
            );
            mensajesEnviados++;

            // Simular carga variable
            std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
        }

        // Actualizar eventos de todos los clientes
        for (auto& pub : publicadores) pub->actualizarEventos();
        for (auto& sub : suscriptores) sub->actualizarEventos();

        auto ahora = std::chrono::high_resolution_clock::now();
        auto duracion = std::chrono::duration_cast<std::chrono::seconds>(ahora - inicio);
        if (duracion.count() >= duracionSegundos) ejecutando = false;
    }

    // Recopilar y mostrar resultados
    TestMetrics metricsTotal;
    for (const auto& pub : publicadores) {
        const auto& m = pub->obtenerMetricas();
        metricsTotal.mensajesEnviados += m.mensajesEnviados;
        metricsTotal.conexionesFallidas += m.conexionesFallidas;
        metricsTotal.latenciaPromedio += m.latenciaPromedio;
    }
    for (const auto& sub : suscriptores) {
        const auto& m = sub->obtenerMetricas();
        metricsTotal.mensajesRecibidos += m.mensajesRecibidos;
        metricsTotal.conexionesFallidas += m.conexionesFallidas;
    }

    // Guardar resultados en archivo
    std::ofstream archivo("resultados_prueba.txt", std::ios::app);
    archivo << "\n=== Resultados de la Prueba ===\n";
    archivo << "Número de clientes: " << numClientes << " publicadores y " << numClientes << " suscriptores\n";
    archivo << "Mensajes enviados: " << metricsTotal.mensajesEnviados << "\n";
    archivo << "Mensajes recibidos: " << metricsTotal.mensajesRecibidos << "\n";
    archivo << "Conexiones fallidas: " << metricsTotal.conexionesFallidas << "\n";
    archivo << "Latencia promedio: " << metricsTotal.latenciaPromedio / publicadores.size() << "ms\n";
    archivo << "Duración de la prueba: " << duracionSegundos << " segundos\n";
    archivo.close();

    // Mostrar resultados en consola
    std::cout << "\n=== Resultados de la Prueba ===\n";
    std::cout << "Mensajes enviados: " << metricsTotal.mensajesEnviados << "\n";
    std::cout << "Mensajes recibidos: " << metricsTotal.mensajesRecibidos << "\n";
    std::cout << "Conexiones fallidas: " << metricsTotal.conexionesFallidas << "\n";
    std::cout << "Latencia promedio: " << metricsTotal.latenciaPromedio / publicadores.size() << "ms\n";
}

int main() {
    std::cout << "Iniciando pruebas de escalabilidad y robustez\n";

    // Prueba 1: Pocos clientes, muchos mensajes
    ejecutarPruebaDeRendimiento(5, 1000, 30);

    // Prueba 2: Muchos clientes, pocos mensajes
    ejecutarPruebaDeRendimiento(20, 100, 30);

    // Prueba 3: Balance entre clientes y mensajes
    ejecutarPruebaDeRendimiento(10, 500, 30);

    std::cout << "\nPruebas completadas. Revisa 'resultados_prueba.txt' para más detalles.\n";
    return 0;
}