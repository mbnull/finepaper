#include "ipcraft/layoutmodel.h"

#include "ipcraft/jsonhelpers.h"

#include <QJsonArray>

namespace {

QJsonObject objectValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isObject() ? value.toObject() : QJsonObject{};
}

void insertObject(QJsonObject& object, const QString& key, const QJsonObject& value) {
    if (!value.isEmpty()) {
        object.insert(key, ipcraft::sortedJsonObject(value));
    }
}

} // namespace

namespace ipcraft {

QJsonObject LayoutView::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("canvas"), sortedJsonObject(canvas));
    insertObject(object, QStringLiteral("properties"), properties);
    insertObject(object, QStringLiteral("native"), native);
    return sortedJsonObject(object);
}

LayoutView LayoutView::fromJson(const QJsonObject& object) {
    LayoutView view;
    view.id = object.value(QStringLiteral("id")).toString();
    view.canvas = objectValue(object, QStringLiteral("canvas"));
    view.properties = objectValue(object, QStringLiteral("properties"));
    view.native = objectValue(object, QStringLiteral("native"));
    return view;
}

QJsonObject LayoutModel::toJson() const {
    QJsonObject object;
    QJsonArray viewArray;
    for (const LayoutView& view : views) {
        viewArray.append(view.toJson());
    }
    object.insert(QStringLiteral("views"), viewArray);
    insertObject(object, QStringLiteral("properties"), properties);
    insertObject(object, QStringLiteral("native"), native);
    return sortedJsonObject(object);
}

LayoutModel LayoutModel::fromJson(const QJsonObject& object) {
    LayoutModel model;
    const QJsonArray viewArray = object.value(QStringLiteral("views")).toArray();
    for (const QJsonValue& viewValue : viewArray) {
        if (viewValue.isObject()) {
            model.views.append(LayoutView::fromJson(viewValue.toObject()));
        }
    }
    model.properties = objectValue(object, QStringLiteral("properties"));
    model.native = objectValue(object, QStringLiteral("native"));
    return model;
}

} // namespace ipcraft
