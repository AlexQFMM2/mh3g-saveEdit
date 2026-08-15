#ifndef WEAPON_MODEL_WIDGET_HPP
#define WEAPON_MODEL_WIDGET_HPP

#include "game_resource_manager.hpp"
#include "mh3g_model.hpp"

#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QSharedPointer>
#include <QStringList>

class QLabel;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QPushButton;

class WeaponModelWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit WeaponModelWidget(QWidget *parent = 0);
    ~WeaponModelWidget();

    void setModel(const QString &modelKey, const QString &arcRelativePath);
    void showItemPlaceholder();
    QString resourceStatus() const;

signals:
    void resourcesChanged();

public slots:
    void resetView();
    void importResources();
    void clearResources();

protected:
    void initializeGL();
    void resizeGL(int width, int height);
    void paintGL();
    void showEvent(QShowEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);

private:
    void requestLoad();
    void acceptModel(int request, const QSharedPointer<Mh3gCpuModel> &model);
    void uploadModel();
    void releaseGpu();
    void setStatus(const QString &text, bool error = false);
    void touchCache(const QString &key, const QSharedPointer<Mh3gCpuModel> &model);
    void trimCache();

    GameResourceManager m_resources;
    QString m_modelKey;
    QString m_arcRelativePath;
    int m_request;
    bool m_glReady;
    bool m_gpuReady;
    QSharedPointer<Mh3gCpuModel> m_model;
    QHash<QString, QSharedPointer<Mh3gCpuModel> > m_cache;
    QStringList m_lru;
    qint64 m_cacheBytes;

    QOpenGLShaderProgram *m_program;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_indexBuffer;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLTexture *m_texture;
    QOpenGLTexture *m_environmentTexture;

    QLabel *m_status;
    QPushButton *m_import;
    QPushButton *m_clear;
    QPushButton *m_reset;
    QPoint m_lastMouse;
    Qt::MouseButton m_dragButton;
    float m_yaw;
    float m_pitch;
    float m_distance;
    QVector3D m_pan;
};

#endif
