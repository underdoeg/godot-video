plugins {
	id("com.android.library")
	id("org.jetbrains.kotlin.android")
}

val pluginName = "gav"
val pluginPackageName = "org.tweaklab.gav.plugin"

android {
	namespace = pluginPackageName
	compileSdk = 36

	defaultConfig {
		manifestPlaceholders += mapOf()
		minSdk = 24

		manifestPlaceholders["godotPluginName"] = pluginName
		manifestPlaceholders["godotPluginPackageName"] = pluginPackageName
		buildConfigField("String", "GODOT_PLUGIN_NAME", "\"${pluginName}\"")
//        setProperty("archivesBaseName", pluginName)
		externalNativeBuild {
			cmake {
				arguments += "-DBUILD_ANDROID=ON -DOUTPUT_NAME=\"${pluginName}\""
				cppFlags += ""
			}
		}
	}

	buildFeatures {
		buildConfig = true
	}

	buildTypes {
		release {
			isMinifyEnabled = false
			buildConfigField("boolean", "DEBUG", "false")
		}

		debug {
			buildConfigField("boolean", "DEBUG", "true")
		}
	}
	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}
	kotlinOptions {
		jvmTarget = "17"
	}
	externalNativeBuild {
		cmake {
			path = file("../../CMakeLists.txt")
			version = "3.22.1"
			buildStagingDirectory = file("./build/cmake")
		}
	}
}

val copyDebugAARToDemoAddons by tasks.registering(Copy::class) {
	description = "Copies the generated debug AAR binary to the plugin's addons directory"
	from("build/outputs/aar")
	include("$pluginName-debug.aar")
	into("../../demo/addons/$pluginName/android")
}

val copyReleaseAARToDemoAddons by tasks.registering(Copy::class) {
	description = "Copies the generated release AAR binary to the plugin's addons directory"
	from("build/outputs/aar")
	include("$pluginName-release.aar")
	into("../../demo/addons/$pluginName/android")
}


tasks.named("assemble").configure {
	finalizedBy(copyDebugAARToDemoAddons)
	finalizedBy(copyReleaseAARToDemoAddons)
}

dependencies {
	implementation("org.godotengine:godot:4.5.0.stable")
}
