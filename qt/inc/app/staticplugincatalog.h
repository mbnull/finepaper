#pragma once

class PluginInteractionRegistry;
class PluginHost;

void registerStaticPlugins(PluginHost& host);
bool registerStaticPluginInteractions(PluginInteractionRegistry& interactions);
