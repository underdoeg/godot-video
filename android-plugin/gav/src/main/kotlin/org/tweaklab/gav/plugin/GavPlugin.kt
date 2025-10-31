package org.tweaklab.gav.plugin

import android.util.Log
import org.godotengine.godot.Godot
import org.godotengine.godot.plugin.GodotPlugin

class GavPlugin(godot: Godot) : GodotPlugin(godot) {


	companion object {
		val TAG = GavPlugin::class.java.simpleName

		init {
			try {
				Log.v(TAG, "Loading ${BuildConfig.GODOT_PLUGIN_NAME} library")
//				System.loadLibrary(BuildConfig.GODOT_PLUGIN_NAME)
				System.loadLibrary("gav_jni")
			} catch (e: UnsatisfiedLinkError) {
				Log.e(TAG, "Unable to load ${BuildConfig.GODOT_PLUGIN_NAME} shared library")
			}
		}
	}


	override fun getPluginName() = BuildConfig.GODOT_PLUGIN_NAME

	override fun getPluginGDExtensionLibrariesPaths() = setOf("${BuildConfig.GODOT_PLUGIN_NAME}.gdextension")


}
